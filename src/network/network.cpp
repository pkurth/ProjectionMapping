#include "pch.h"
#include "network.h"
#include "core/log.h"

#include <winsock2.h>

bool initializeNetwork()
{
	WSADATA wsaData;

	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) 
	{
		LOG_ERROR("Network could not be initialized");
		return false;
	}

	return true;
}

void shutdownNetwork()
{
	WSACleanup();
}
