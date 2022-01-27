#pragma once

#include "network.h"

// Arguments are: 
// - data 
// - data-size 
typedef std::function<void(const char*, uint32)> client_message_callback;

bool startNetworkClient(const char* serverIP, uint32 serverPort, const client_message_callback& callback);

bool sendToServer(const char* data, uint32 size);
