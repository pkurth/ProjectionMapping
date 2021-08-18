#pragma once

#include "core/camera.h"
#include "window/dx_window.h"


struct projector_base
{
	virtual void edit() = 0;
	bool active() { return window.open; }


protected:
	void activate();
	void deactivate();

	render_camera camera;
	dx_window window;
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

