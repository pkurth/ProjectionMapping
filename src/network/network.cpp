#include "pch.h"
#include "network.h"
#include "core/log.h"

#include <ws2tcpip.h>
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

bool getLocalIPAddress(char out[128])
{
	char ac[80];
	if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR)
	{
		LOG_ERROR("Failed to get local host name");
		return false;
	}

	addrinfo* addrInfo;
	int err = getaddrinfo(ac, 0, 0, &addrInfo);
	if (err != 0)
	{
		LOG_ERROR("Failed to get address info: %s\n", gai_strerrorA(err));
		return false;
	}

	bool addressFound = false;

	for (addrinfo* p = addrInfo; p != 0; p = p->ai_next)
	{
		in_addr* addr;
		if (p->ai_family == NETWORK_FAMILY)
		{
			if (p->ai_family == AF_INET)
			{
				sockaddr_in* ipv = (sockaddr_in*)p->ai_addr;
				addr = &(ipv->sin_addr);
			}
			else
			{
				struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
				addr = (in_addr*)&(ipv6->sin6_addr);
			}

			inet_ntop(p->ai_family, addr, out, 128);

			addressFound = true;
			break;
		}
	}

	freeaddrinfo(addrInfo);

	return addressFound;
}
