#pragma once

#include "network.h"

struct network_client
{
	network_client();
	~network_client() { shutdown(); }

	bool initialize(const char* addressToConnectTo, uint32 portToConnectTo);
	void shutdown();

	bool send(const void* data, uint32 size);
	template <typename T> bool send(const T& data) { return send((void*)&data, sizeof(T)); }

private:
	uint64 connectSocket;
};
