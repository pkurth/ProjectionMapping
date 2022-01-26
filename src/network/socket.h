#pragma once

#include "network.h"

#include <winsock2.h>

struct network_address
{
	bool initialize(const char* ip, uint32 port);

#if NETWORK_FAMILY == AF_INET
	sockaddr_in addr = {};
#else
	sockaddr_in6 addr = {};
#endif
};

struct network_socket
{
	bool initialize(uint32 port); // Pass 0 if you don't care.
	void close();

	bool isOpen();

	bool send(const network_address& destination, const void* data, uint32 size);
	uint32 receive(network_address& sender, void* data, uint32 maxSize);

	SOCKET socket = INVALID_SOCKET;
};
