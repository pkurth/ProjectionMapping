#pragma once

#include "projector_solver.h"
#include "scene/scene.h"
#include "network/socket.h"

struct projector_manager;


struct projector_instantiation
{
	uint32 clientID;
	uint32 monitorIndex;
	projector_calibration calibration;
};

struct client_calibration_message
{
	uint32 monitorIndex;
	projector_calibration calibration;
};

struct projector_network_server
{
	bool initialize(game_scene& scene, projector_manager* manager, uint32 port, char* outIP);
	bool update(float dt);

	bool broadcastObjectInfo();
	bool broadcastProjectors(const std::vector<projector_instantiation>& instantiations);

private:
	
	struct client_connection
	{
		uint16 clientID;
		network_address address;
	};

	bool createObjectMessage(struct send_buffer& buffer);
	bool createSettingsMessage(struct send_buffer& buffer);
	bool createObjectUpdateMessage(struct send_buffer& buffer);
	bool createViewerCameraUpdateMessage(struct send_buffer& buffer);
	bool createProjectorInstantiationMessage(struct send_buffer& buffer, const std::vector<projector_instantiation>& instantiations);

	bool sendToAllClients(struct send_buffer& buffer);
	bool sendToClient(struct send_buffer& buffer, const client_connection& connection);

	std::vector<client_connection> clientConnections;
	network_socket serverSocket;

	uint16 runningClientID = 0;
	uint32 runningMessageID = 1;

	float timeSinceLastUpdate = 0.f;
	const float updateTime = 1.f / 30.f;

	game_scene* scene;
	projector_manager* manager;
	projector_solver_settings oldSolverSettings;
};

struct projector_network_client
{
	bool initialize(game_scene& scene, projector_manager* manager, const char* serverIP, uint32 serverPort);
	bool update();

	bool sendHello();
	bool reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calibs);

	uint32 clientID = -1;

private:
	bool sendToServer(struct send_buffer& buffer);

	game_scene* scene;
	projector_manager* manager;

	network_address serverAddress;
	network_socket clientSocket;

	bool connected = false;


	uint32 latestSettingsMessageID = 0;
	uint32 latestObjectMessageID = 0;
	uint32 latestObjectUpdateMessageID = 0;
	uint32 latestViewerCameraUpdateMessageID = 0;
	uint32 latestProjectorInstantiationMessageID = 0;

	projector_solver_settings oldSolverSettings;


	std::unordered_map<uint32, scene_entity> objectIDToEntity;
};


struct projector_network_protocol
{
	bool start(game_scene& scene, projector_manager* manager, bool isServer);
	bool update(float dt);


	bool server_broadcastObjectInfo();
	bool server_broadcastProjectors(const std::vector<projector_instantiation>& instantiations);

	bool client_reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calibs);
	uint32 client_getID();

	bool initialized = false;

	bool isServer;

#if NETWORK_FAMILY == AF_INET
	char serverIP[128] = "131.188.49.110";
#else
	char serverIP[128] = "fe80::96b:37d3:ee41:b0a";
#endif

	uint32 serverPort = 27015;

private:
	game_scene* scene;
	projector_manager* manager;

	projector_network_server server;
	projector_network_client client;
};
