#pragma once

#include "network.h"


bool startNetworkClient(const char* serverIP, uint32 serverPort, const network_message_callback& callback);
