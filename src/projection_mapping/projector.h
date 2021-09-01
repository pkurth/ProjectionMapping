#pragma once

#include "core/camera.h"
#include "window/dx_window.h"
#include "projector_renderer.h"


struct projector_base
{
	virtual void edit() = 0;
	bool active() { return window.open; }

	uint64 render(const opaque_render_pass* opaqueRenderPass, const directional_light& sun, const ref<pbr_environment>& environment, const render_camera& viewerCamera);

protected:
	void editCommon(const std::string& name, uint32 width, uint32 height);

	void activate();
	void deactivate();

	render_camera camera;
	dx_window window;

	projector_renderer renderer;
};

struct physical_projector : projector_base
{
	physical_projector(const monitor_info& monitor);

	void edit() override;

	monitor_info monitor;
};

struct dummy_projector : projector_base
{
	dummy_projector(std::string name, vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intr);

	void edit() override;

	std::string name;
};

