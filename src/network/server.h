#pragma once

#include "network.h"
#include "address.h"

// Arguments are: 
// - data 
// - data-size 
// - client address (use for responses)
// - is client already known or not

typedef std::function<void(const char*, uint32, const network_address&, bool)> server_message_callback;

bool startNetworkServer(uint32 port, const server_message_callback& callback);
bool sendTo(const network_address& address, const char* data, uint32 size);
bool broadcastToClients(const char* data, uint32 size);
