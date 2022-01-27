#pragma once

#include "network.h"

#include <winsock2.h>
#include <ws2tcpip.h>

struct network_address
{
	bool initialize(const char* ip, uint32 port);

#if NETWORK_FAMILY == AF_INET
	sockaddr_in addr = {};
#else
	sockaddr_in6 addr = {};
#endif
};

bool operator==(const network_address& a, const network_address& b);
inline bool operator!=(const network_address& a, const network_address& b) { return !(a == b); }


