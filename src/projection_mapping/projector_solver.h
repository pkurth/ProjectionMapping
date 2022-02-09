#pragma once

#include "dx/dx_texture.h"
#include "dx/dx_command_list.h"
#include "core/math.h"
#include "rendering/render_pass.h"
#include "rendering/material.h"
#include "projector.h"


enum projector_mode
{
	projector_mode_calibration,
	projector_mode_projection_mapping,
};

static const char* projectorModeNames[] =
{
	"Calibration",
	"Projection mapping",
};

struct projector_solver_settings
{
	bool applySolverIntensity = false;

	float referenceDistance = 0.5f;
	float referenceWhite = 0.7f;

	float depthDiscontinuityThreshold = 0.09f;
	uint32 depthDiscontinuityDilateRadius = 2;
	uint32 depthDiscontinuitySmoothRadius = 8;

	float colorDiscontinuityThreshold = 0.4f;
	uint32 colorDiscontinuityDilateRadius = 8;
	uint32 colorDiscontinuitySmoothRadius = 8;

	float colorMaskStrength = 0.7f;

	projector_mode mode = projector_mode_projection_mapping;
};

struct projector_solver
{
	void initialize();

	void solve(const projector_component* projectors, const render_camera* cameras, uint32 numProjectors);

	projector_solver_settings settings;

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

			dx_double_descriptor_handle bestMaskSRVBaseDescriptor;
			dx_double_descriptor_handle bestMaskUAVBaseDescriptor;

			dx_double_descriptor_handle depthDiscontinuitiesTexturesBaseDescriptor;
			dx_double_descriptor_handle colorDiscontinuitiesTexturesBaseDescriptor;
			   
			dx_double_descriptor_handle intensitiesSRVBaseDescriptor;
			dx_double_descriptor_handle intensitiesUAVBaseDescriptor;
			   
			dx_double_descriptor_handle tempIntensitiesSRVBaseDescriptor;
			dx_double_descriptor_handle tempIntensitiesUAVBaseDescriptor;

			dx_double_descriptor_handle attenuationSRVBaseDescriptor;
			dx_double_descriptor_handle attenuationUAVBaseDescriptor;

			dx_double_descriptor_handle maskSRVBaseDescriptor;
			dx_double_descriptor_handle maskUAVBaseDescriptor;
		};

		dx_double_descriptor_handle descriptors[16];
	};

	D3D12_GPU_VIRTUAL_ADDRESS projectorsGPUAddress;

	uint32 numProjectors;

	dx_pushable_descriptor_heap heaps[NUM_BUFFERED_FRAMES];


	friend struct visualize_intensities_pipeline;
	friend struct simulate_projectors_pipeline;
};
