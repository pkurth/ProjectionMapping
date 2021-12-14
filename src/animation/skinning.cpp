#include "pch.h"
#include "skinning.h"
#include "dx/dx_command_list.h"
#include "dx/dx_barrier_batcher.h"
#include "dx/dx_profiling.h"
#include "rendering/material.h"
#include "dx/dx_buffer.h"

#include "skinning_rs.hlsli"

#define MAX_NUM_SKINNING_MATRICES_PER_FRAME 4096
#define MAX_NUM_SKINNED_VERTICES_PER_FRAME (1024 * 256)

static ref<dx_buffer> skinningMatricesBuffer; // Buffered frames are in a single dx_buffer.

static uint32 currentSkinnedVertexBuffer;
static vertex_buffer_group skinnedVertexBuffer[2]; // We have two of these, so that we can compute screen space velocities.

static dx_pipeline skinningPipeline;
static dx_pipeline clothSkinningPipeline;


struct skinning_call
{
	dx_vertex_buffer_group_view vertexBuffer;
	vertex_range range;
	uint32 jointOffset;
	uint32 numJoints;
	uint32 vertexOffset;
};

struct cloth_skinning_call
{
	dx_vertex_buffer_view vertexBuffer;
	uint32 gridSizeX;
	uint32 gridSizeY;
	uint32 vertexOffset;
};

static mat4 skinningMatrices[MAX_NUM_SKINNING_MATRICES_PER_FRAME];
static volatile uint32 numSkinningMatricesThisFrame;

static skinning_call calls[1024];
static volatile uint32 numCalls;

static cloth_skinning_call clothCalls[128];
static volatile uint32 numClothCalls;


static volatile uint32 totalNumVertices;


void initializeSkinning()
{
	skinningMatricesBuffer = createUploadBuffer(sizeof(mat4), MAX_NUM_SKINNING_MATRICES_PER_FRAME * NUM_BUFFERED_FRAMES, 0);

	for (uint32 i = 0; i < 2; ++i)
	{
		skinnedVertexBuffer[i].positions = createVertexBuffer(sizeof(vec3), MAX_NUM_SKINNED_VERTICES_PER_FRAME, 0, true);
		skinnedVertexBuffer[i].others = createVertexBuffer(getVertexSize(mesh_creation_flags_with_positions | mesh_creation_flags_with_uvs | mesh_creation_flags_with_normals | mesh_creation_flags_with_tangents), MAX_NUM_SKINNED_VERTICES_PER_FRAME, 0, true);
	}

	skinningPipeline = createReloadablePipeline("skinning_cs");
	clothSkinningPipeline = createReloadablePipeline("cloth_skinning_cs");
}

std::tuple<dx_vertex_buffer_group_view, mat4*> skinObject(const dx_vertex_buffer_group_view& vertexBuffer, vertex_range range, uint32 numJoints)
{
	uint32 jointOffset = atomicAdd(numSkinningMatricesThisFrame, numJoints);
	assert(jointOffset + numJoints <= MAX_NUM_SKINNING_MATRICES_PER_FRAME);

	uint32 vertexOffset = atomicAdd(totalNumVertices, range.numVertices);
	assert(vertexOffset + range.numVertices <= MAX_NUM_SKINNED_VERTICES_PER_FRAME);

	uint32 callIndex = atomicIncrement(numCalls);
	assert(callIndex < arraysize(calls));

	calls[callIndex] = {
		vertexBuffer,
		range,
		jointOffset,
		numJoints,
		vertexOffset
	};

	uint32 numVertices = range.numVertices;

	auto positions = skinnedVertexBuffer[currentSkinnedVertexBuffer].positions;
	auto others = skinnedVertexBuffer[currentSkinnedVertexBuffer].others;

	dx_vertex_buffer_group_view result;
	result.positions.view.BufferLocation = positions->gpuVirtualAddress + positions->elementSize * vertexOffset;
	result.positions.view.SizeInBytes = positions->elementSize * numVertices;
	result.positions.view.StrideInBytes = positions->elementSize;
	result.others.view.BufferLocation = others->gpuVirtualAddress + others->elementSize * vertexOffset;
	result.others.view.SizeInBytes = others->elementSize * numVertices;
	result.others.view.StrideInBytes = others->elementSize;

	return { result, skinningMatrices + jointOffset };
}

std::tuple<dx_vertex_buffer_group_view, mat4*> skinObject(const dx_vertex_buffer_group_view& vertexBuffer, uint32 numVertices, uint32 numJoints)
{
	auto [vb, mats] = skinObject(vertexBuffer, vertex_range{ 0, numVertices }, numJoints);
	return { vb, mats };
}

std::tuple<dx_vertex_buffer_group_view, mat4*> skinObject(const dx_vertex_buffer_group_view& vertexBuffer, submesh_info submesh, uint32 numJoints)
{
	auto [vb, mats] = skinObject(vertexBuffer, vertex_range{ submesh.baseVertex, submesh.numVertices }, numJoints);

	return { vb, mats };
}

dx_vertex_buffer_group_view skinCloth(const dx_vertex_buffer_view& inpositions, uint32 gridSizeX, uint32 gridSizeY)
{
	uint32 numVertices = gridSizeX * gridSizeY;
	uint32 vertexOffset = atomicAdd(totalNumVertices, numVertices);
	assert(vertexOffset + numVertices <= MAX_NUM_SKINNED_VERTICES_PER_FRAME);

	uint32 callIndex = atomicIncrement(numClothCalls);
	assert(callIndex < arraysize(clothCalls));

	clothCalls[callIndex] = {
		inpositions,
		gridSizeX,
		gridSizeY,
		vertexOffset
	};

	auto positions = skinnedVertexBuffer[currentSkinnedVertexBuffer].positions;
	auto others = skinnedVertexBuffer[currentSkinnedVertexBuffer].others;

	dx_vertex_buffer_group_view result;
	result.positions.view.BufferLocation = positions->gpuVirtualAddress + positions->elementSize * vertexOffset;
	result.positions.view.SizeInBytes = positions->elementSize * numVertices;
	result.positions.view.StrideInBytes = positions->elementSize;
	result.others.view.BufferLocation = others->gpuVirtualAddress + others->elementSize * vertexOffset;
	result.others.view.SizeInBytes = others->elementSize * numVertices;
	result.others.view.StrideInBytes = others->elementSize;
	return result;
}

uint64 performSkinning()
{
	uint64 result = 0;

	// TODO: We currently make no attempt to ensure that all draw calls have actually been written completely, if executed on another thread. 
	if (numCalls > 0 || numClothCalls > 0)
	{
		//dx_command_list* cl = dxContext.getFreeComputeCommandList(true);
		dx_command_list* cl = dxContext.getFreeRenderCommandList();

		{
			PROFILE_ALL(cl, "Skinning");

			if (numCalls)
			{
				PROFILE_ALL(cl, "Skeletal");

				uint32 matrixOffset = dxContext.bufferedFrameID * MAX_NUM_SKINNING_MATRICES_PER_FRAME;

				mat4* mats = (mat4*)mapBuffer(skinningMatricesBuffer, false);
				memcpy(mats + matrixOffset, skinningMatrices, sizeof(mat4) * numSkinningMatricesThisFrame);
				unmapBuffer(skinningMatricesBuffer, true, map_range{ matrixOffset, numSkinningMatricesThisFrame });


				cl->setPipelineState(*skinningPipeline.pipeline);
				cl->setComputeRootSignature(*skinningPipeline.rootSignature);

				cl->setRootComputeSRV(SKINNING_RS_MATRICES, skinningMatricesBuffer->gpuVirtualAddress + sizeof(mat4) * matrixOffset);
				cl->setRootComputeUAV(SKINNING_RS_OUTPUT0, skinnedVertexBuffer[currentSkinnedVertexBuffer].positions);
				cl->setRootComputeUAV(SKINNING_RS_OUTPUT1, skinnedVertexBuffer[currentSkinnedVertexBuffer].others);

				for (uint32 i = 0; i < numCalls; ++i)
				{
					auto& c = calls[i];
					cl->setRootComputeSRV(SKINNING_RS_INPUT_VERTEX_BUFFER0, c.vertexBuffer.positions.view.BufferLocation);
					cl->setRootComputeSRV(SKINNING_RS_INPUT_VERTEX_BUFFER1, c.vertexBuffer.others.view.BufferLocation);
					cl->setCompute32BitConstants(SKINNING_RS_CB, skinning_cb{ c.jointOffset, c.numJoints, c.range.firstVertex, c.range.numVertices, c.vertexOffset });
					cl->dispatch(bucketize(c.range.numVertices, 512));
				}
			}
			if (numClothCalls)
			{
				PROFILE_ALL(cl, "Cloth");

				cl->setPipelineState(*clothSkinningPipeline.pipeline);
				cl->setComputeRootSignature(*clothSkinningPipeline.rootSignature);

				cl->setRootComputeUAV(CLOTH_SKINNING_RS_OUTPUT0, skinnedVertexBuffer[currentSkinnedVertexBuffer].positions);
				cl->setRootComputeUAV(CLOTH_SKINNING_RS_OUTPUT1, skinnedVertexBuffer[currentSkinnedVertexBuffer].others);

				for (uint32 i = 0; i < numClothCalls; ++i)
				{
					auto& c = clothCalls[i];
					cl->setRootComputeSRV(CLOTH_SKINNING_RS_INPUT, c.vertexBuffer.view.BufferLocation);
					cl->setCompute32BitConstants(CLOTH_SKINNING_RS_CB, cloth_skinning_cb{ c.gridSizeX, c.gridSizeY, c.vertexOffset });
					cl->dispatch(bucketize(c.gridSizeX, 15), bucketize(c.gridSizeY, 15)); // 15 is correct here.
				}
			}

			// Not necessary, since the command list ends here.
			//barrier_batcher(cl)
			//	.uav(skinnedVertexBuffer[currentSkinnedVertexBuffer].positions)
			//	.uav(skinnedVertexBuffer[currentSkinnedVertexBuffer].others);
		}

		result = dxContext.executeCommandList(cl);
	}

	currentSkinnedVertexBuffer = 1 - currentSkinnedVertexBuffer;
	::numCalls = 0;
	::numClothCalls = 0;
	numSkinningMatricesThisFrame = 0;
	totalNumVertices = 0;

	return result;
}


