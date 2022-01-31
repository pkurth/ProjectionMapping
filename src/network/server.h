#pragma once

#include "network.h"
#include "address.h"

bool startNetworkServer(uint32 port);
receive_result checkForServerMessages(char* buffer, uint32 size, uint32& outBytesReceived, network_address& outClientAddress, bool& outClientKnown);


bool sendTo(const network_address& address, const char* data, uint32 size);
bool broadcastToClients(const char* data, uint32 size);
