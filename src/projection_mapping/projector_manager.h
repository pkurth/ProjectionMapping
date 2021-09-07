#pragma once

#include "projector.h"
#include "projector_solver.h"

struct projector_manager
{
	projector_manager();

	void beginFrame();
	void updateAndRender();

	void setViewerCamera(const render_camera& camera) { this->viewerCamera = camera; }
	void setEnvironment(const ref<pbr_environment>& environment) { this->environment = environment; }
	void setSun(const directional_light& light) { this->sun = light; this->sun.numShadowCascades = 0; }

	void submitRenderPass(const opaque_render_pass* renderPass) { assert(!opaqueRenderPass); opaqueRenderPass = renderPass; }

	void renderProjectorFrusta(ldr_render_pass* renderPass);

private:
	void addDummyProjector();

	std::vector<monitor_info> monitors;
	std::vector<physical_projector> physicalProjectors;
	std::vector<dummy_projector> dummyProjectors;

	const opaque_render_pass* opaqueRenderPass;

	render_camera viewerCamera;
	ref<pbr_environment> environment;
	directional_light sun;

	bool applySolverIntensity = false;
};
