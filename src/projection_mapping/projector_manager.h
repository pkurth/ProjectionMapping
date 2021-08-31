#pragma once

#include "projector.h"

struct projector_manager
{
	projector_manager();

	void beginFrame();
	void updateAndRender(const render_camera& viewerCamera);

	std::vector<monitor_info> monitors;
	std::vector<physical_projector> physicalProjectors;
	std::vector<dummy_projector> dummyProjectors;

	opaque_render_pass opaqueRenderPass;
};
