#include "pch.h"
#include "projector_solver.h"

#include "dx/dx_pipeline.h"
#include "dx/dx_descriptor_allocation.h"

#include "projector_rs.hlsli"

static dx_pipeline solverPipeline;
static dx_pushable_descriptor_heap heaps[NUM_BUFFERED_FRAMES];

void initializeProjectorSolver()
{
	solverPipeline = createReloadablePipeline("projector_solver_cs");

	for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
	{
		heaps[i].initialize(1024);
	}
}

uint64 solveProjectorIntensities(const std::vector<projector_solver_input>& input, uint32 iterations)
{
	uint32 numProjectors = (uint32)input.size();

	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];
	heap.reset();

	dx_allocation alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(mat4));
	mat4* viewProjs = (mat4*)alloc.cpuPtr;

	dx_gpu_descriptor_handle renderResultsBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		*viewProjs++ = in.viewProj;
		heap.push().create2DTextureSRV(in.renderResult);
	}

	dx_gpu_descriptor_handle depthTexturesBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().createDepthTextureSRV(in.depthBuffer);
	}

	dx_gpu_descriptor_handle intensitiesSRVBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().create2DTextureSRV(in.outIntensities);
	}

	dx_gpu_descriptor_handle intensitiesUAVBaseDescriptor = heap.currentGPU;
	for (const projector_solver_input& in : input)
	{
		heap.push().create2DTextureUAV(in.outIntensities);
	}


	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	cl->setPipelineState(*solverPipeline.pipeline);
	cl->setComputeRootSignature(*solverPipeline.rootSignature);

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setRootComputeSRV(PROJECTOR_SOLVER_RS_VIEWPROJS, alloc.gpuPtr);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_RENDER_RESULTS, renderResultsBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
	cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_INTENSITIES, intensitiesSRVBaseDescriptor);

	for (uint32 iter = 0; iter < iterations; ++iter)
	{
		for (uint32 proj = 0; proj < numProjectors; ++proj)
		{
			projector_solver_cb cb;
			cb.currentIndex = proj;
			cb.numProjectors = numProjectors;
			cb.width = input[proj].renderResult->width;
			cb.height = input[proj].renderResult->height;

			cl->transitionBarrier(input[proj].outIntensities, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cl->setCompute32BitConstants(PROJECTOR_SOLVER_RS_CB, cb);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_CURRENT_INTENSITY, intensitiesUAVBaseDescriptor + proj);

			cl->dispatch(bucketize(cb.width, PROJECTOR_SOLVER_BLOCK_SIZE), bucketize(cb.height, PROJECTOR_SOLVER_BLOCK_SIZE));
			cl->uavBarrier(input[proj].outIntensities);
			cl->transitionBarrier(input[proj].outIntensities, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

	cl->resetToDynamicDescriptorHeap();

	return dxContext.executeCommandList(cl);
}
