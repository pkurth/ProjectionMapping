#pragma once

#include "projector.h"

struct projector_manager
{
	projector_manager();

	void update();

	std::vector<monitor_info> monitors;
	std::vector<physical_projector> physicalProjectors;
	std::vector<dummy_projector> dummyProjectors;
};
