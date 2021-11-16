#pragma once

#include "rgbd_camera.h"

struct depth_tracker
{
	static void initializeCommon();

	static std::vector<rgbd_camera_info> allConnectedRGBDCameras;
};

