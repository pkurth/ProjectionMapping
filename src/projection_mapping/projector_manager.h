#pragma once

#include "projector.h"
#include "projector_solver.h"
#include "scene/scene.h"

struct projector_manager
{
	projector_manager(game_scene& scene);

	void beginFrame();
	void updateAndRender();

	projector_solver solver;

private:
	game_scene* scene;

	bool detailWindowOpen = false;
};
