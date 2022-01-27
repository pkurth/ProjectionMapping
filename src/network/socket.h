#pragma once

#include "address.h"

struct network_socket
{
	bool initialize(uint32 port, bool blocking = true); // Pass 0 as port if you don't care.
	void close();

	bool isOpen();

	bool send(const network_address& destination, const void* data, uint32 size);
	uint32 receive(network_address& sender, void* data, uint32 maxSize);

	SOCKET socket = INVALID_SOCKET;
};
