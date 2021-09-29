#pragma once

#include "core/camera.h"
#include "window/dx_window.h"
#include "projector_renderer.h"


struct projector_component
{
	projector_component()
	{
		uint32 width = 1280;
		uint32 height = 800;
		window.initialize(TEXT("Projector"), width, height);

		camera.initializeCalibrated(vec3(0.f, 0.f, 0.f), quat::identity, width, height, camera_intrinsics{700.f, 700.f, width * 0.5f, height * 0.5f }, 0.01f);

		static const vec4 colorTable[] =
		{
			vec4(1.f, 0.f, 0.f, 1.f),
			vec4(0.f, 1.f, 0.f, 1.f),
			vec4(0.f, 0.f, 1.f, 1.f),
			vec4(1.f, 1.f, 0.f, 1.f),
			vec4(0.f, 1.f, 1.f, 1.f),
			vec4(1.f, 0.f, 1.f, 1.f),
		};
		static uint32 index = 0;

		frustumColor = colorTable[index];
		index = (index + 1) % arraysize(colorTable);

		renderer.initialize(color_depth_8, window.clientWidth, window.clientHeight);
	}

	projector_component(projector_component&&) = default;
	projector_component& operator=(const projector_component&) = delete;
	projector_component& operator=(projector_component&& ) = default;
	
	~projector_component()
	{
		window.shutdown();
		renderer.shutdown();
	}

	void updateMonitor(monitor_info* monitor)
	{
		this->monitor = monitor;
		if (monitor)
		{
			window.moveToMonitor(*monitor);
			if (!window.fullscreen)
			{
				window.toggleFullscreen();
			}
		}
	}

	monitor_info* monitor = 0; // If null, this is a dummy projector.
	projector_renderer renderer;
	dx_window window;
	render_camera camera;
	vec4 frustumColor;
};

