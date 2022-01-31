#pragma once

#include "network.h"

bool startNetworkClient(const char* serverIP, uint32 serverPort);
receive_result checkForClientMessages(char* buffer, uint32 size, uint32& outBytesReceived);

bool sendToServer(const char* data, uint32 size);
