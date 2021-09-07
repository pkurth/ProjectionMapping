#pragma once

#include "dx/dx_texture.h"
#include "dx/dx_command_list.h"
#include "core/math.h"
#include "rendering/render_pass.h"

struct projector_solver_input
{
	ref<dx_texture> renderResult;
	ref<dx_texture> worldNormals;
	ref<dx_texture> depthBuffer;
	ref<dx_texture> outIntensities;
	mat4 viewProj;
	vec3 position;
};

void initializeProjectorSolver();
void solveProjectorIntensities(const std::vector<projector_solver_input>& input, uint32 iterations);

void visualizeProjectorIntensities(opaque_render_pass* opaqueRenderPass, 
	const mat4& transform,
	const material_vertex_buffer_group_view& vertexBuffer,
	const material_index_buffer_view& indexBuffer,
	submesh_info submesh,
	uint32 objectID = -1);
