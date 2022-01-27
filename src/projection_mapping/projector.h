#pragma once

#include "core/camera.h"
#include "window/dx_window.h"
#include "projector_renderer.h"


struct projector_component
{
	projector_component() { }

	projector_component(const render_camera& camera, int32 computerID, const std::string& monitorID, bool fullscreen)
	{
		initialize(camera, computerID, monitorID, fullscreen);
	}

	void initialize(const render_camera& camera, int32 computerID, const std::string& monitorID, bool fullscreen)
	{
		shutdown();

		this->computerID = computerID;
		this->monitorID = monitorID;

		calibratedCamera = camera;

		if (!headless())
		{
			window.initialize(TEXT("Projector"), camera.width, camera.height, color_depth_8, false, true);

			bool movedToCorrectMonitor = false;
			for (auto& monitor : win32_window::allConnectedMonitors)
			{
				if (monitor.uniqueID == monitorID)
				{
					window.moveToMonitor(monitor);
					movedToCorrectMonitor = true;
					break;
				}
			}

			if (movedToCorrectMonitor)
			{
				if (fullscreen)
				{
					window.toggleFullscreen();
				}
			}
		}

		frustumColor = frustumColors[computerID + 1]; // Computer ID is -1 based.

		renderer.initialize(color_depth_8, camera.width, camera.height);
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

	inline bool headless() { return computerID != myComputerID; }
	inline bool shouldPresent() { return !headless() && window.open && window.clientWidth > 0 && window.clientHeight > 0; }

	static inline int32 myComputerID = -1; // Server is -1;

	int32 computerID;
	std::string monitorID;

	projector_renderer renderer;
	dx_window window;
	render_camera calibratedCamera;
	vec4 frustumColor;

	static inline vec4 frustumColors[] =
	{
		{ 1.f, 0.f, 0.f, 1.f },
		{ 0.f, 1.f, 0.f, 1.f },
		{ 0.f, 0.f, 1.f, 1.f },
		{ 1.f, 1.f, 0.f, 1.f },
		{ 0.f, 1.f, 1.f, 1.f },
	};
};

