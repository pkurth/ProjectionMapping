#include "pch.h"
#include "projector_solver.h"
#include "geometry/geometry.h"
#include "rendering/render_utils.h"

#include "dx/dx_pipeline.h"
#include "dx/dx_descriptor_allocation.h"
#include "dx/dx_profiling.h"
#include "dx/dx_barrier_batcher.h"

#include "projector_rs.hlsli"
#include "transform.hlsli"

static dx_pipeline solverPipeline;
static dx_pipeline regularizePipeline;
static dx_pipeline visualizeIntensitiesPipeline;
static dx_pipeline projectorSimulationPipeline;


void projector_solver::initialize()
{
	solverPipeline = createReloadablePipeline("projector_solver_cs");
	regularizePipeline = createReloadablePipeline("projector_regularize_cs");

	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.renderTargets(opaqueLightPassFormats, arraysize(opaqueLightPassFormats), depthStencilFormat)
			.depthSettings(true, false, D3D12_COMPARISON_FUNC_EQUAL);

		visualizeIntensitiesPipeline = createReloadablePipeline(desc, { "default_vs", "projector_intensity_visualization_ps" });
		projectorSimulationPipeline = createReloadablePipeline(desc, { "default_vs", "projector_simulation_ps" });
	}

	for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
	{
		heaps[i].initialize(1024);
	}
}

static void projectorToCB(const render_camera& camera, projector_cb& proj)
{
	proj.viewProj = camera.viewProj;
	proj.invViewProj = camera.invViewProj;
	proj.position = vec4(camera.position, 1.f);
	proj.screenDims = vec2((float)camera.width, (float)camera.height);
	proj.invScreenDims = 1.f / proj.screenDims;
	proj.projectionParams = camera.getShaderProjectionParams();
	proj.forward = vec4(camera.rotation * vec3(0.f, 0.f, -1.f), 0.f);
}

void projector_solver::prepareForFrame(const projector_component* projectors, uint32 numProjectors)
{
	this->numProjectors = numProjectors;

	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];
	heap.reset();

	dx_allocation alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(projector_cb));
	projector_cb* viewProjs = (projector_cb*)alloc.cpuPtr;
	viewProjsGPUAddress = alloc.gpuPtr;

	alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(projector_cb));
	projector_cb* realViewProjs = (projector_cb*)alloc.cpuPtr;
	realViewProjsGPUAddress = alloc.gpuPtr;

	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];

		projectorToCB(p.calibratedCamera, *viewProjs++);
		projectorToCB(p.realCamera, *realViewProjs++);
		
		widths[i] = p.calibratedCamera.width;
		heights[i] = p.calibratedCamera.height;
		intensityTextures[i] = p.renderer.solverIntensityTexture.get();
		intensityTempTextures[i] = p.renderer.solverIntensityTempTexture.get();
	}


	linearRenderResultsBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureSRV(p.renderer.ldrPostProcessingTexture);
	}

	srgbRenderResultsBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureSRV(p.renderer.frameResult);
	}

	worldNormalsBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureSRV(p.renderer.worldNormalsTexture);
	}

	depthTexturesBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().createDepthTextureSRV(p.renderer.depthStencilBuffer);
	}

	realDepthTexturesBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().createDepthTextureSRV(p.renderer.realDepthStencilBuffer);
	}

	depthDiscontinuitiesTexturesBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().createDepthTextureSRV(p.renderer.depthDiscontinuitiesTexture);
	}

	intensitiesSRVBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureSRV(p.renderer.solverIntensityTexture);
	}

	intensitiesUAVBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureUAV(p.renderer.solverIntensityTexture);
	}

	tempIntensitiesSRVBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureSRV(p.renderer.solverIntensityTempTexture);
	}

	tempIntensitiesUAVBaseDescriptor = heap.currentGPU;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];
		heap.push().create2DTextureUAV(p.renderer.solverIntensityTempTexture);
	}
}

void projector_solver::solve()
{
	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];

	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);


	for (uint32 iter = 0; iter < numIterationsPerFrame; ++iter)
	{
		DX_PROFILE_BLOCK(cl, "Solver iteration");

		for (uint32 proj = 0; proj < numProjectors; ++proj)
		{
			DX_PROFILE_BLOCK(cl, "Single projector");


			uint32 width = widths[proj];
			uint32 height = heights[proj];


			// Solve intensity.

			cl->setPipelineState(*solverPipeline.pipeline);
			cl->setComputeRootSignature(*solverPipeline.rootSignature);

			cl->setRootComputeSRV(PROJECTOR_SOLVER_RS_VIEWPROJS, viewProjsGPUAddress);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_RENDER_RESULTS, linearRenderResultsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_WORLD_NORMALS, worldNormalsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_INTENSITIES, intensitiesSRVBaseDescriptor); // Read from regularized intensities.
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_MASKS, depthDiscontinuitiesTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_OUT_INTENSITIES, tempIntensitiesUAVBaseDescriptor); // Write to temp intensities.

			projector_solver_cb solverCB;
			solverCB.currentIndex = proj;
			solverCB.numProjectors = numProjectors;
			solverCB.referenceDistance = referenceDistance;

			cl->transitionBarrier(intensityTempTextures[proj]->resource, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cl->setCompute32BitConstants(PROJECTOR_SOLVER_RS_CB, solverCB);

			cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));

			barrier_batcher(cl)
				//.uav(intensityTempTextures[proj]->resource)
				.transition(intensityTempTextures[proj]->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)
				.transition(intensityTextures[proj]->resource, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);



			// Regularize.

			cl->setPipelineState(*regularizePipeline.pipeline);
			cl->setComputeRootSignature(*regularizePipeline.rootSignature);

			projector_regularize_cb regularizeCB;
			regularizeCB.currentIndex = proj;
			regularizeCB.strength = regularizationStrength;

			cl->setCompute32BitConstants(PROJECTOR_REGULARIZE_RS_CB, regularizeCB);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_INTENSITIES, tempIntensitiesSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_OUT_INTENSITIES, intensitiesUAVBaseDescriptor);

			cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));

			barrier_batcher(cl)
				//.uav(intensityTextures[proj]->resource)
				.transition(intensityTextures[proj]->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

	cl->resetToDynamicDescriptorHeap();

	dxContext.executeCommandList(cl);
}













struct visualization_material 
{
	projector_solver* solver;
};

struct visualize_intensities_pipeline
{
	using material_t = visualization_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(visualize_intensities_pipeline)
{
	cl->setPipelineState(*visualizeIntensitiesPipeline.pipeline);
	cl->setGraphicsRootSignature(*visualizeIntensitiesPipeline.rootSignature);
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

PIPELINE_RENDER_IMPL(visualize_intensities_pipeline)
{
	projector_solver& solver = *rc.material.solver;

	dx_pushable_descriptor_heap& heap = solver.heaps[dxContext.bufferedFrameID];

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setGraphics32BitConstants(PROJECTOR_INTENSITY_VISUALIZATION_RS_TRANSFORM, transform_cb{ viewProj * rc.transform, rc.transform });
	cl->setGraphics32BitConstants(PROJECTOR_INTENSITY_VISUALIZATION_RS_CB, projector_visualization_cb{ solver.numProjectors, solver.referenceDistance });
	cl->setRootGraphicsSRV(PROJECTOR_INTENSITY_VISUALIZATION_RS_VIEWPROJS, solver.viewProjsGPUAddress);
	cl->setGraphicsDescriptorTable(PROJECTOR_INTENSITY_VISUALIZATION_RS_DEPTH_TEXTURES, solver.depthTexturesBaseDescriptor);
	cl->setGraphicsDescriptorTable(PROJECTOR_INTENSITY_VISUALIZATION_RS_INTENSITIES, solver.intensitiesSRVBaseDescriptor);


	cl->setVertexBuffer(0, rc.vertexBuffer.positions);
	cl->setVertexBuffer(1, rc.vertexBuffer.others);
	cl->setIndexBuffer(rc.indexBuffer);
	cl->drawIndexed(rc.submesh.numIndices, 1, rc.submesh.firstIndex, rc.submesh.baseVertex, 0);

	cl->resetToDynamicDescriptorHeap();
}





struct simulate_projectors_pipeline
{
	using material_t = visualization_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(simulate_projectors_pipeline)
{
	cl->setPipelineState(*projectorSimulationPipeline.pipeline);
	cl->setGraphicsRootSignature(*projectorSimulationPipeline.rootSignature);
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

PIPELINE_RENDER_IMPL(simulate_projectors_pipeline)
{
	projector_solver& solver = *rc.material.solver;

	dx_pushable_descriptor_heap& heap = solver.heaps[dxContext.bufferedFrameID];

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setGraphics32BitConstants(PROJECTOR_SIMULATION_RS_TRANSFORM, transform_cb{ viewProj * rc.transform, rc.transform });
	cl->setGraphics32BitConstants(PROJECTOR_SIMULATION_RS_CB, projector_visualization_cb{ solver.numProjectors, solver.referenceDistance });
	cl->setRootGraphicsSRV(PROJECTOR_SIMULATION_RS_VIEWPROJS, solver.realViewProjsGPUAddress);
	cl->setGraphicsDescriptorTable(PROJECTOR_SIMULATION_RS_RENDER_RESULTS, solver.srgbRenderResultsBaseDescriptor);
	cl->setGraphicsDescriptorTable(PROJECTOR_SIMULATION_RS_DEPTH_TEXTURES, solver.realDepthTexturesBaseDescriptor);


	cl->setVertexBuffer(0, rc.vertexBuffer.positions);
	cl->setVertexBuffer(1, rc.vertexBuffer.others);
	cl->setIndexBuffer(rc.indexBuffer);
	cl->drawIndexed(rc.submesh.numIndices, 1, rc.submesh.firstIndex, rc.submesh.baseVertex, 0);

	cl->resetToDynamicDescriptorHeap();
}








void projector_solver::visualizeProjectorIntensities(opaque_render_pass* opaqueRenderPass,
	const mat4& transform,
	const dx_vertex_buffer_group_view& vertexBuffer,
	const dx_index_buffer_view& indexBuffer,
	submesh_info submesh,
	uint32 objectID)
{
	opaqueRenderPass->renderStaticObject<visualize_intensities_pipeline>(
		transform, vertexBuffer, indexBuffer,
		submesh, { this },
		objectID
		);
}

void projector_solver::simulateProjectors(opaque_render_pass* opaqueRenderPass,
	const mat4& transform,
	const dx_vertex_buffer_group_view& vertexBuffer,
	const dx_index_buffer_view& indexBuffer,
	submesh_info submesh,
	uint32 objectID)
{
	opaqueRenderPass->renderStaticObject<simulate_projectors_pipeline>(
		transform, vertexBuffer, indexBuffer,
		submesh, { this },
		objectID
		);
}
