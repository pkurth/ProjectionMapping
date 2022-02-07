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

	void reportLocalCalibration(const std::string& monitor, camera_intrinsics intrinsics, uint32 width, uint32 height, vec3 position, quat rotation);

	projector_solver solver;
	projector_context context;

private:

	std::unordered_set<std::string> remoteMonitors;

	void createProjectors(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors);
	void createProjector(const std::string& monitorID, bool local);

	std::vector<std::string> getLocalProjectors();
	std::vector<std::string> getRemoteProjectors();

	void notifyClients(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors);

	game_scene* scene;

	bool detailWindowOpen = false;

	bool isServer = true;
};
