#pragma once

#include "address.h"
#include "network.h"

struct network_socket
{
	bool initialize(uint32 port, bool blocking = true); // Pass 0 as port if you don't care.
	void close();

	bool isOpen();

	bool send(const network_address& destination, const void* data, uint32 size);
	receive_result receive(network_address& sender, void* data, uint32 maxSize, uint32& outBytesReceived);

	SOCKET socket = INVALID_SOCKET;
};
