#pragma once

#include "network.h"

// Arguments are: 
// - data 
// - data-size 
typedef std::function<void(const char*, uint32)> client_message_callback;

typedef std::function<void()> client_close_callback;

bool startNetworkClient(const char* serverIP, uint32 serverPort, const client_message_callback& messageCallback, const client_close_callback& closeCallback);

bool sendToServer(const char* data, uint32 size);
