#pragma once

#include "core/camera.h"
#include "core/log.h"
#include "window/dx_window.h"
#include "window/software_window.h"
#include "projector_renderer.h"


struct projector_calibration
{
	quat rotation;
	vec3 position;

	uint32 width, height;

	camera_intrinsics intrinsics;
};

struct projector_context
{
	std::unordered_map<std::string, projector_calibration> knownProjectorCalibrations; // Maps monitor IDs to projector descriptions.
};

struct projector_component
{
	projector_component() { }

	projector_component(uint32 width, uint32 height, camera_intrinsics intrinsics, const std::string& monitorID, bool local, bool fullscreen)
	{
		initialize(width, height, intrinsics, monitorID, local, fullscreen);
	}

	void initialize(uint32 width, uint32 height, camera_intrinsics intrinsics, const std::string& monitorID, bool local, bool fullscreen)
	{
		shutdown();

		this->monitorID = monitorID;
		this->intrinsics = intrinsics;
		this->width = width;
		this->height = height;

		if (local)
		{
			bool monitorFound = false;
			monitor_info monitor;
			for (auto& m : win32_window::allConnectedMonitors)
			{
				if (m.uniqueID == monitorID)
				{
					monitor = m;
					monitorFound = true;
					break;
				}
			}

			window.initialize(TEXT("Projector"), width, height, color_depth_8, false, true);
			window.setIcon("assets/icons/projector.png");

			headless = false;

			if (monitorFound)
			{
				window.moveToMonitor(monitor);
				if (fullscreen)
				{
					window.toggleFullscreen();
				}

				frustumColor = vec4(0.f, 1.f, 0.f, 1.f);
				LOG_MESSAGE("Created projector on monitor '%s'", monitorID.c_str());
			}
			else
			{
				frustumColor = vec4(1.f, 0.f, 0.f, 1.f);
				LOG_WARNING("Monitor '%s' was not found. Projector window could not be moved", monitorID.c_str());
			}
		}
		else
		{
			headless = true;
			frustumColor = vec4(1.f, 1.f, 0.f, 1.f);

			LOG_MESSAGE("Created headless (remote) projector");
		}

		renderer.initialize(color_depth_8, width, height);
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

	inline bool shouldPresent() { return !headless && window.open && window.clientWidth > 0 && window.clientHeight > 0; }

	std::string monitorID;
	bool headless;

	uint32 width, height;
	projector_renderer renderer;
	camera_intrinsics intrinsics;
	vec4 frustumColor;

	dx_window window;

	static inline vec4 frustumColors[] =
	{
		{ 1.f, 0.f, 0.f, 1.f },
		{ 0.f, 1.f, 0.f, 1.f },
		{ 0.f, 0.f, 1.f, 1.f },
		{ 1.f, 1.f, 0.f, 1.f },
		{ 0.f, 1.f, 1.f, 1.f },
	};
};

