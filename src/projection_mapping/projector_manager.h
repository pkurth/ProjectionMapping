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

	void onMessageFromClient(const std::vector<std::string>& remoteMonitors);
	void onMessageFromServer(std::unordered_map<std::string, projector_calibration>&& calibrations, const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors);

	projector_solver solver;
	projector_context context;

private:

	std::unordered_set<std::string> remoteMonitors;

	void createProjectors(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors);
	void createProjector(const std::string& monitorID, bool local);

	void sendInformationToClients();

	game_scene* scene;

	bool detailWindowOpen = false;

	bool isServer = true;
	bool dirty = false;

	std::mutex mutex;

};
