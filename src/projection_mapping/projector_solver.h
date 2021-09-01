#pragma once

#include "dx/dx_texture.h"
#include "dx/dx_command_list.h"
#include "core/math.h"

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
uint64 solveProjectorIntensities(const std::vector<projector_solver_input>& input, uint32 iterations);

