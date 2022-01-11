#pragma once

#include "core/camera.h"
#include "window/dx_window.h"
#include "projector_renderer.h"


struct projector_component
{
	projector_component()
	{
		initialize();
	}

	projector_component(const render_camera& camera)
	{
		initialize(camera);
	}

	void initialize()
	{
		shutdown();

		uint32 width = 1920;
		uint32 height = 1080;
		window.initialize(TEXT("Projector"), width, height);

		//calibratedCamera.initializeCalibrated(vec3(0.f, 0.f, 0.f), quat::identity, width, height, camera_intrinsics{700.f, 700.f, width * 0.5f, height * 0.5f }, 0.01f);
		calibratedCamera.initializeIngame(vec3(0.f, 0.f, 0.f), quat::identity, deg2rad(60.f), 0.01f);
		realCamera = calibratedCamera;

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

	void initialize(const render_camera& camera)
	{
		shutdown();

		calibratedCamera = camera;
		realCamera = calibratedCamera;

		window.initialize(TEXT("Projector"), camera.width, camera.height);

		frustumColor = vec4(1.f, 0.f, 0.f, 1.f);

		renderer.initialize(color_depth_8, window.clientWidth, window.clientHeight);
	}

	void shutdown()
	{
		window.shutdown();
		renderer.shutdown();
	}

	projector_component(projector_component&&) = default;
	projector_component& operator=(const projector_component&) = delete;
	projector_component& operator=(projector_component&&) = default;
	
	~projector_component()
	{
		shutdown();
	}

	projector_renderer renderer;
	dx_window window;
	render_camera calibratedCamera;
	render_camera realCamera; // This is used to simulate calibration errors. This represents the real intrinsics/extrinsics of the projector.
	vec4 frustumColor;
};

