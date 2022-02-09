#pragma once

#include "projector_solver.h"
#include "scene/scene.h"
#include "network/socket.h"

struct projector_manager;

struct projector_network_server
{
	bool initialize(game_scene& scene, projector_manager* manager, uint32 port, char* outIP);
	bool update(float dt);

private:

	bool sendToAllClients(struct send_buffer& buffer);

	struct client_connection
	{
		std::string name;
		uint16 clientID;
		network_address address;
	};

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

private:
	game_scene* scene;
	projector_manager* manager;

	network_address serverAddress;
	network_socket clientSocket;

	bool connected = false;
	uint16 clientID = -1;


	uint32 latestSettingsMessageID = 0;
};


struct projector_network_protocol
{
	bool start(game_scene& scene, projector_manager* manager, bool isServer);
	bool update(float dt);

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
