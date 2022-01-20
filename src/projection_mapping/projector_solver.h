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

	void solve(const projector_component* projectors, uint32 numProjectors);

	uint32 numIterationsPerFrame = 1;
	float referenceDistance = 0.5f;
	float regularizationStrength = 1.f;
	float depthDiscontinuityMaskStrength = 1.f;
	bool simulateCalibrationError = false;

	void simulateProjectors(opaque_render_pass* opaqueRenderPass,
		const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		uint32 objectID = -1);

private:
	union
	{
		struct
		{
			dx_double_descriptor_handle linearRenderResultsBaseDescriptor;
			dx_double_descriptor_handle srgbRenderResultsBaseDescriptor;
			   
			dx_double_descriptor_handle worldNormalsBaseDescriptor;
			dx_double_descriptor_handle depthTexturesBaseDescriptor;
			dx_double_descriptor_handle realDepthTexturesBaseDescriptor; // For simulation.

			dx_double_descriptor_handle bestMaskSRVBaseDescriptor;
			dx_double_descriptor_handle bestMaskUAVBaseDescriptor;

			dx_double_descriptor_handle depthDiscontinuitiesTexturesBaseDescriptor;
			dx_double_descriptor_handle colorDiscontinuitiesTexturesBaseDescriptor;
			   
			dx_double_descriptor_handle intensitiesSRVBaseDescriptor;
			dx_double_descriptor_handle intensitiesUAVBaseDescriptor;
			   
			dx_double_descriptor_handle tempIntensitiesSRVBaseDescriptor;
			dx_double_descriptor_handle tempIntensitiesUAVBaseDescriptor;

			dx_double_descriptor_handle confidencesSRVBaseDescriptor;
			dx_double_descriptor_handle confidencesUAVBaseDescriptor;
		};

		dx_double_descriptor_handle descriptors[15];
	};

	D3D12_GPU_VIRTUAL_ADDRESS projectorsGPUAddress;
	D3D12_GPU_VIRTUAL_ADDRESS realProjectorsGPUAddress; // For simulation.

	uint32 numProjectors;

	dx_pushable_descriptor_heap heaps[NUM_BUFFERED_FRAMES];


	friend struct visualize_intensities_pipeline;
	friend struct simulate_projectors_pipeline;
};
