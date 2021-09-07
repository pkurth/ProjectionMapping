#include "pch.h"
#include "projector_solver.h"
#include "geometry/geometry.h"
#include "rendering/render_utils.h"

#include "dx/dx_pipeline.h"
#include "dx/dx_descriptor_allocation.h"

#include "projector_rs.hlsli"
#include "transform.hlsli"

static dx_pipeline solverPipeline;
static dx_pipeline visualizeIntensitiesPipeline;

static dx_pushable_descriptor_heap heaps[NUM_BUFFERED_FRAMES];

void initializeProjectorSolver()
{
	solverPipeline = createReloadablePipeline("projector_solver_cs");

	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.renderTargets(opaqueLightPassFormats, arraysize(opaqueLightPassFormats), depthStencilFormat)
			.depthSettings(true, false, D3D12_COMPARISON_FUNC_EQUAL);

		visualizeIntensitiesPipeline = createReloadablePipeline(desc, { "default_vs", "projector_intensity_visualization_ps" });
	}

	for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
	{
		heaps[i].initialize(1024);
	}
}

static dx_gpu_descriptor_handle renderResultsBaseDescriptor;
static dx_gpu_descriptor_handle worldNormalsBaseDescriptor;
static dx_gpu_descriptor_handle depthTexturesBaseDescriptor;
static dx_gpu_descriptor_handle intensitiesSRVBaseDescriptor;
static D3D12_GPU_VIRTUAL_ADDRESS viewProjsGPUAddress;
static uint32 numProjectors;

void solveProjectorIntensities(const std::vector<projector_solver_input>& input, uint32 iterations)
{
	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	numProjectors = (uint32)input.size();

	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];
	heap.reset();

	dx_allocation alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(projector_cb));
	projector_cb* viewProjs = (projector_cb*)alloc.cpuPtr;
	viewProjsGPUAddress = alloc.gpuPtr;

	renderResultsBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		projector_cb& proj = *viewProjs++;
		proj.viewProj = in.viewProj;
		proj.invViewProj = invert(in.viewProj);
		proj.position = vec4(in.position, 1.f);
		proj.screenDims = vec2((float)in.renderResult->width, (float)in.renderResult->height);
		proj.invScreenDims = 1.f / proj.screenDims;

		heap.push().create2DTextureSRV(in.renderResult);
	}

	worldNormalsBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().create2DTextureSRV(in.worldNormals);
	}

	depthTexturesBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().createDepthTextureSRV(in.depthBuffer);
	}

	intensitiesSRVBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().create2DTextureSRV(in.outIntensities);
	}

	dx_gpu_descriptor_handle intensitiesUAVBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().create2DTextureUAV(in.outIntensities);
	}


	cl->setPipelineState(*solverPipeline.pipeline);
	cl->setComputeRootSignature(*solverPipeline.rootSignature);

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setRootComputeSRV(PROJECTOR_SOLVER_RS_VIEWPROJS, viewProjsGPUAddress);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_RENDER_RESULTS, renderResultsBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_WORLD_NORMALS, worldNormalsBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_INTENSITIES, intensitiesSRVBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_OUT_INTENSITIES, intensitiesUAVBaseDescriptor);

	for (uint32 iter = 0; iter < iterations; ++iter)
	{
		for (uint32 proj = 0; proj < numProjectors; ++proj)
		{
			projector_solver_cb cb;
			cb.currentIndex = proj;
			cb.numProjectors = numProjectors;

			uint32 width = input[proj].renderResult->width;
			uint32 height = input[proj].renderResult->height;

			cl->transitionBarrier(input[proj].outIntensities, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cl->setCompute32BitConstants(PROJECTOR_SOLVER_RS_CB, cb);

			cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));
			cl->uavBarrier(input[proj].outIntensities);
			cl->transitionBarrier(input[proj].outIntensities, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

	cl->resetToDynamicDescriptorHeap();

	dxContext.executeCommandList(cl);
}

struct visualize_intensities_material {};

struct visualize_intensities_pipeline
{
	using material_t = visualize_intensities_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(visualize_intensities_pipeline)
{
	cl->setPipelineState(*visualizeIntensitiesPipeline.pipeline);
	cl->setGraphicsRootSignature(*visualizeIntensitiesPipeline.rootSignature);
}

PIPELINE_RENDER_IMPL(visualize_intensities_pipeline)
{
	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setGraphics32BitConstants(PROJECTOR_INTENSITY_VISUALIZATION_RS_TRANSFORM, transform_cb{ viewProj * rc.transform, rc.transform });
	cl->setGraphics32BitConstants(PROJECTOR_INTENSITY_VISUALIZATION_RS_CB, numProjectors);
	cl->setRootGraphicsSRV(PROJECTOR_INTENSITY_VISUALIZATION_RS_VIEWPROJS, viewProjsGPUAddress);
	cl->setGraphicsDescriptorTable(PROJECTOR_INTENSITY_VISUALIZATION_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
	cl->setGraphicsDescriptorTable(PROJECTOR_INTENSITY_VISUALIZATION_RS_INTENSITIES, intensitiesSRVBaseDescriptor);


	cl->setVertexBuffer(0, rc.vertexBuffer.positions);
	cl->setVertexBuffer(1, rc.vertexBuffer.others);
	cl->setIndexBuffer(rc.indexBuffer);
	cl->drawIndexed(rc.submesh.numIndices, 1, rc.submesh.firstIndex, rc.submesh.baseVertex, 0);

	cl->resetToDynamicDescriptorHeap();
}

void visualizeProjectorIntensities(opaque_render_pass* opaqueRenderPass, 
	const mat4& transform,
	const material_vertex_buffer_group_view& vertexBuffer,
	const material_index_buffer_view& indexBuffer,
	submesh_info submesh,
	uint32 objectID)
{
	opaqueRenderPass->renderStaticObject<visualize_intensities_pipeline>(
		transform, vertexBuffer, indexBuffer,
		submesh, {},
		objectID
		);
}
