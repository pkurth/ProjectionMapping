#pragma once

#include "network.h"


bool startNetworkServer(uint32 port, const network_message_callback& callback);

bool broadcastMessageToClients(const void* data, uint64 size);

