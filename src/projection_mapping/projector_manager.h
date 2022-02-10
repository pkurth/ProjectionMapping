#pragma once

#include "projector.h"
#include "projector_solver.h"
#include "scene/scene.h"
#include "projector_network_protocol.h"

#include <unordered_set>

struct depth_tracker;

struct projector_manager
{
	projector_manager(game_scene& scene, depth_tracker* tracker);

	void beginFrame();
	void updateAndRender(float dt);

	void onSceneLoad();

	projector_solver solver;
	projector_context context;


	void reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calib);


	static constexpr uint32 MAX_NUM_PROJECTORS = 4;
	bool isProjectorIndex[MAX_NUM_PROJECTORS] = {};

private:

	depth_tracker* tracker;

	// Network callbacks.
	void network_newClient(const std::string& hostname, uint32 clientID, const std::vector<std::string>& descriptions, const std::vector<std::string>& uniqueIDs);
	void network_clientCalibration(uint32 clientID, const std::vector<client_calibration_message>& calibrations);

	void network_projectorInstantiations(const std::vector<projector_instantiation>& instantiations);

	struct client_monitor
	{
		std::string description;
		std::string uniqueID;
	};

	struct client_info
	{
		uint32 clientID;
		std::string hostname;
		std::vector<client_monitor> monitors;
	};

	std::unordered_map<uint32, client_info> clients;


	void loadSetup();
	void saveSetup();

	software_window blackWindows[MAX_NUM_PROJECTORS];

	void createProjectorsAndNotify();
	std::vector<projector_instantiation> createInstantiations();

	void createProjectors(const std::vector<projector_instantiation>& instantiations);
	void createProjector(const std::string& monitorID, bool local, const projector_calibration& calib);

	game_scene* scene;

	bool detailWindowOpen = false;

	bool isServer = true;

	projector_network_protocol protocol;

	friend struct projector_network_server;
	friend struct projector_network_client;
};
