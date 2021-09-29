#pragma once

#include "dx/dx_texture.h"
#include "dx/dx_command_list.h"
#include "core/math.h"
#include "rendering/render_pass.h"
#include "rendering/material.h"
#include "projector.h"

struct projector_solver
{
	void initialize();

	void prepareForFrame(const projector_component* projectors, uint32 numProjectors);
	void solve();

	uint32 numIterationsPerFrame = 1;


	void visualizeProjectorIntensities(opaque_render_pass* opaqueRenderPass,
		const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		uint32 objectID = -1);

	void simulateProjectors(opaque_render_pass* opaqueRenderPass,
		const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		uint32 objectID = -1);

private:
	dx_gpu_descriptor_handle linearRenderResultsBaseDescriptor;
	dx_gpu_descriptor_handle srgbRenderResultsBaseDescriptor;

	dx_gpu_descriptor_handle worldNormalsBaseDescriptor;
	dx_gpu_descriptor_handle depthTexturesBaseDescriptor;
	dx_gpu_descriptor_handle depthDiscontinuitiesTexturesBaseDescriptor;

	dx_gpu_descriptor_handle intensitiesSRVBaseDescriptor;
	dx_gpu_descriptor_handle intensitiesUAVBaseDescriptor;

	D3D12_GPU_VIRTUAL_ADDRESS viewProjsGPUAddress;
	uint32 numProjectors;

	uint32 widths[16];
	uint32 heights[16];
	dx_texture* intensityTextures[16];

	dx_pushable_descriptor_heap heaps[NUM_BUFFERED_FRAMES];


	friend struct visualize_intensities_pipeline;
	friend struct simulate_projectors_pipeline;
};
