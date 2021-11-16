#include "pch.h"
#include "tracking.h"

std::vector<rgbd_camera_info> depth_tracker::allConnectedRGBDCameras;

void depth_tracker::initializeCommon()
{
	allConnectedRGBDCameras = enumerateRGBDCameras();
}


