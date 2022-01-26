#pragma once

#include "network.h"


bool startNetworkClient(const char* addressToConnectTo, uint32 portToConnectTo, const network_message_callback& callback);

bool sendMessageToServer(const void* data, uint64 size);

