#pragma once

#include "projector.h"
#include "projector_solver.h"
#include "scene/scene.h"

#include <unordered_set>

struct projector_manager
{
	projector_manager(game_scene& scene);

	void beginFrame();
	void updateAndRender(float dt);

	void onSceneLoad();

	projector_context context;
	projector_solver solver;

private:

	void getMyProjectors();
	void createProjectors();
	void createProjector(const std::string& monitorID, bool local);

	game_scene* scene;

	bool detailWindowOpen = false;

	bool isServer = true;


	std::unordered_set<std::string> myProjectors;
	std::unordered_set<std::string> remoteProjectors;
};
