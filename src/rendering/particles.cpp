#include "pch.h"
#include "particles.h"
#include "core/camera.h"
#include "geometry/geometry.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"
#include "dx/dx_barrier_batcher.h"
#include "dx/dx_profiling.h"
#include "bitonic_sort.h"


ref<dx_buffer> particle_system::particleDrawCommandBuffer;
dx_command_signature particle_system::particleCommandSignature;
dx_mesh particle_system::billboardMesh;

static dx_pipeline startPipeline;

static volatile uint32 particleSystemCounter = 0;


void particle_system::initializePipeline()
{
	startPipeline = createReloadablePipeline("particle_start_cs");

	particleDrawCommandBuffer = createBuffer(sizeof(particle_draw), 1024, 0, true);

	D3D12_INDIRECT_ARGUMENT_DESC argumentDesc;
	argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	particleCommandSignature = createCommandSignature({}, &argumentDesc, 1, sizeof(particle_draw));

	cpu_mesh cpuMesh(mesh_creation_flags_with_positions);
	submesh_info submesh = cpuMesh.pushQuad(1.f);
	billboardMesh = cpuMesh.createDXMesh();
}

void particle_system::initializeInternal(uint32 particleStructSize, uint32 maxNumParticles, float emitRate, submesh_info submesh, sort_mode sortMode)
{
	this->maxNumParticles = maxNumParticles;
	this->emitRate = emitRate;
	this->index = atomicIncrement(particleSystemCounter);
	this->submesh = submesh;
	this->sortMode = sortMode;


	std::vector<uint32> dead(maxNumParticles);

	for (uint32 i = 0; i < maxNumParticles; ++i)
	{
		dead[i] = i;
	}

	particle_counters counters = {};
	counters.numDeadParticles = maxNumParticles;


	particlesBuffer = createBuffer(particleStructSize, maxNumParticles, 0, true);


	// Lists.
	listBuffer = createBuffer(1, 
		sizeof(particle_counters)
		+ maxNumParticles * 3 * sizeof(uint32),
		0, true);
	updateBufferDataRange(listBuffer, &counters, 0, sizeof(particle_counters));
	updateBufferDataRange(listBuffer, dead.data(), getDeadListOffset(), maxNumParticles * sizeof(uint32));


	// Dispatch.
	dispatchBuffer = createBuffer((uint32)sizeof(particle_dispatch), 1, 0, true);


	if (sortMode != sort_mode_none)
	{
		sortBuffer = createBuffer(sizeof(float), maxNumParticles, 0, true);
	}

	
	particle_draw draw = {};
	draw.arguments.BaseVertexLocation = submesh.baseVertex;
	draw.arguments.IndexCountPerInstance = submesh.numTriangles * 3;
	draw.arguments.StartIndexLocation = submesh.firstTriangle * 3;
	updateBufferDataRange(particleDrawCommandBuffer, &draw, (uint32)sizeof(particle_draw) * index, (uint32)sizeof(particle_draw));
}

void particle_system::initializeAsBillboard(uint32 particleStructSize, uint32 maxNumParticles, float emitRate, sort_mode sortMode)
{
	submesh_info submesh = { 2, 0, 0, 6 };
	initializeInternal(particleStructSize, maxNumParticles, emitRate, submesh, sortMode);
}

void particle_system::initializeAsMesh(uint32 particleStructSize, dx_mesh mesh, submesh_info submesh, uint32 maxNumParticles, float emitRate, sort_mode sortMode)
{
	initializeInternal(particleStructSize, maxNumParticles, emitRate, submesh, sortMode);
}

particle_draw_info particle_system::getDrawInfo(const struct dx_pipeline& renderPipeline)
{
	particle_draw_info result;
	result.particleBuffer = particlesBuffer;
	result.aliveList = listBuffer;
	result.aliveListOffset = getAliveListOffset(currentAlive);
	result.commandBuffer = particleDrawCommandBuffer;
	result.commandBufferOffset = index * particleDrawCommandBuffer->elementSize;
	result.rootParameterOffset = renderPipeline.rootSignature->totalNumParameters - PARTICLE_RENDERING_RS_COUNT;
	return result;
}

void particle_system::update(float dt, const dx_pipeline& emitPipeline, const dx_pipeline& simulatePipeline)
{
	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	{
		DX_PROFILE_BLOCK(cl, "Particle system update");

		// Buffers get promoted to D3D12_RESOURCE_STATE_UNORDERED_ACCESS implicitly, so we can omit this.
		//cl->transitionBarrier(dispatchBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		particle_sim_cb cb = { emitRate, dt, submesh.numTriangles * 3, submesh.firstTriangle * 3, submesh.baseVertex };

		// ----------------------------------------
		// START
		// ----------------------------------------

		{
			DX_PROFILE_BLOCK(cl, "Start");

			cl->setPipelineState(*startPipeline.pipeline);
			cl->setComputeRootSignature(*startPipeline.rootSignature);

			cl->setRootComputeUAV(PARTICLE_COMPUTE_RS_DISPATCH_INFO, dispatchBuffer);
			setResources(cl, cb, 0, false);

			cl->dispatch(1);
			barrier_batcher(cl)
				//.uav(dispatchBuffer)
				.uav(particleDrawCommandBuffer)
				.transition(dispatchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		}

		// ----------------------------------------
		// EMIT
		// ----------------------------------------

		{
			DX_PROFILE_BLOCK(cl, "Emit");

			cl->setPipelineState(*emitPipeline.pipeline);
			cl->setComputeRootSignature(*emitPipeline.rootSignature);

			uint32 numUserRootParameters = getNumUserRootParameters(emitPipeline);
			setResources(cl, cb, numUserRootParameters, true);

			cl->dispatchIndirect(1, dispatchBuffer, 0);
			barrier_batcher(cl)
				.uav(particleDrawCommandBuffer)
				.uav(particlesBuffer)
				.uav(listBuffer);
		}

		// ----------------------------------------
		// SIMULATE
		// ----------------------------------------

		{
			DX_PROFILE_BLOCK(cl, "Simulate");

			cl->setPipelineState(*simulatePipeline.pipeline);
			cl->setComputeRootSignature(*simulatePipeline.rootSignature);

			uint32 numUserRootParameters = getNumUserRootParameters(simulatePipeline);
			setResources(cl, cb, numUserRootParameters, true);

			cl->dispatchIndirect(1, dispatchBuffer, sizeof(D3D12_DISPATCH_ARGUMENTS));
			barrier_batcher(cl)
				.uav(particleDrawCommandBuffer)
				.uav(particlesBuffer)
				.uav(listBuffer)
				.uav(sortBuffer);
		}

		currentAlive = 1 - currentAlive;

		// ----------------------------------------
		// SORT
		// ----------------------------------------

		if (sortMode != sort_mode_none)
		{
			barrier_batcher(cl)
				.transition(particleDrawCommandBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			bitonicSortFloat(cl, 
				sortBuffer, 0,
				listBuffer, getAliveListOffset(currentAlive),
				maxNumParticles,
				particleDrawCommandBuffer, sizeof(particle_draw) * index + offsetof(particle_draw, arguments) + offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, InstanceCount),
				sortMode == sort_mode_front_to_back);
		}


		// Buffers decay to D3D12_RESOURCE_STATE_COMMON implicitly, so we can omit this.
		//cl->transitionBarrier(dispatchBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
	}

	dxContext.executeCommandList(cl);
}

void particle_system::setResources(dx_command_list* cl, const particle_sim_cb& cb, uint32 offset, bool setUserResources)
{
	uint32 nextAlive = 1 - currentAlive;

	cl->setCompute32BitConstants(offset + PARTICLE_COMPUTE_RS_CB, cb);
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_DRAW_INFO, particleDrawCommandBuffer->gpuVirtualAddress + sizeof(particle_draw) * index);
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_COUNTERS, listBuffer->gpuVirtualAddress);
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_PARTICLES, particlesBuffer);
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_DEAD_LIST, listBuffer->gpuVirtualAddress + getDeadListOffset());
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_CURRENT_ALIVE, listBuffer->gpuVirtualAddress + getAliveListOffset(currentAlive));
	cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_NEW_ALIVE, listBuffer->gpuVirtualAddress + getAliveListOffset(nextAlive));

	if (sortBuffer)
	{
		cl->setRootComputeUAV(offset + PARTICLE_COMPUTE_RS_SORT_KEY, sortBuffer);
	}

	if (setUserResources)
	{
		setSimulationParameters(cl);
	}
}

uint32 particle_system::getAliveListOffset(uint32 alive)
{
	assert(alive == 0 || alive == 1);

	return getDeadListOffset() +
		(1 + alive) * maxNumParticles * (uint32)sizeof(uint32);
}

uint32 particle_system::getDeadListOffset()
{
	return (uint32)sizeof(particle_counters);
}

uint32 particle_system::getNumUserRootParameters(const dx_pipeline& pipeline)
{
	uint32 numSimulationRootParameters = PARTICLE_COMPUTE_RS_COUNT;
	uint32 numUserRootParameters = pipeline.rootSignature->totalNumParameters - numSimulationRootParameters;
	return numUserRootParameters;
}
