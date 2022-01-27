#include "pch.h"
#include "address.h"
#include "core/log.h"

#include <ws2tcpip.h>


bool network_address::initialize(const char* ip, uint32 port)
{
#if NETWORK_FAMILY == AF_INET
	sockaddr_in addr = {};
	addr.sin_family = NETWORK_FAMILY;
	addr.sin_port = htons(port); // Convert to network order.
	if (inet_pton(NETWORK_FAMILY, ip, &addr.sin_addr) != 1)
	{
		LOG_ERROR("Failed to convert address: %d\n", WSAGetLastError());
		return false;
	}
#else
	sockaddr_in6 addr = {};
	addr.sin6_family = NETWORK_FAMILY;
	addr.sin6_port = htons(port); // Convert to network order.
	if (inet_pton(NETWORK_FAMILY, ip, &addr.sin6_addr) != 1)
	{
		LOG_ERROR("Failed to convert address: %d\n", WSAGetLastError());
		return false;
	}
#endif

	this->addr = addr;

	return true;
}

bool operator==(const network_address& a, const network_address& b)
{
	return memcmp(&a.addr, &b.addr, sizeof(a.addr)) == 0;
}
