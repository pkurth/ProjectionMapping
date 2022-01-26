#pragma once

#include "network.h"
#include <functional>

typedef std::function<void(void*, uint32)> server_callback;

struct network_server
{
	network_server();
	~network_server() { shutdown(); }

	bool initialize(uint32 port);
	void shutdown();

	bool run();

	void registerReceiveFunction(server_callback&& callback) { this->callback = std::move(callback); }

private:

	struct active_connection
	{
		uint64 socket;
	};

	std::vector<active_connection> connections;

	server_callback callback;

	uint32 port;
	uint64 listenSocket;

	std::thread serverThread;
};
