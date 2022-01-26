#pragma once

#include <functional>

#define NETWORK_BUFFER_SIZE 512

typedef std::function<void(const char*, uint32)> network_message_callback;

// AF_INET  for IPv4
// AF_INET6 for IPv6
#define NETWORK_FAMILY AF_INET

enum network_message_type
{
	network_message_hello,


	network_message_user_defined,
};


bool initializeNetwork();
void shutdownNetwork();

bool getLocalIPAddress(char out[128]);
