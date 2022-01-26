#pragma once

#include "network.h"


bool startNetworkServer(uint32 port, const network_message_callback& callback);
bool broadcastToClients(const char* data, uint32 size);
