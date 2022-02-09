#pragma once

#include "projector.h"
#include "projector_solver.h"
#include "scene/scene.h"
#include "projector_network_protocol.h"

#include <unordered_set>


struct projector_manager
{
	projector_manager(game_scene& scene);

	void beginFrame();
	void updateAndRender(float dt);

	void onSceneLoad();

	projector_solver solver;
	projector_context context;

	static constexpr uint32 MAX_NUM_PROJECTORS = 4;
	bool isProjectorIndex[MAX_NUM_PROJECTORS] = {};

private:

	// Network callbacks.
	void network_newClient(const std::string& hostname, uint32 clientID, const std::vector<std::string>& descriptions, const std::vector<std::string>& uniqueIDs);


	struct client_monitor
	{
		std::string description;
		std::string uniqueID;
	};

	struct client_info
	{
		uint32 clientID;
		std::vector<client_monitor> monitors;
	};

	std::unordered_map<std::string, client_info> clients;


	void loadSetup();
	void saveSetup();

	software_window blackWindows[MAX_NUM_PROJECTORS];

	std::unordered_set<std::string> remoteMonitors;

	void createProjectors(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors);
	void createProjector(const std::string& monitorID, bool local);

	std::vector<std::string> getLocalProjectors();
	std::vector<std::string> getRemoteProjectors();

	game_scene* scene;

	bool detailWindowOpen = false;

	bool isServer = true;

	projector_network_protocol protocol;

	friend struct projector_network_server;
	friend struct projector_network_client;
};
