#pragma once

#include <functional>

#define NETWORK_BUFFER_SIZE 4096

// AF_INET  for IPv4
// AF_INET6 for IPv6
#define NETWORK_FAMILY AF_INET


bool initializeNetwork();
void shutdownNetwork();

bool getLocalIPAddress(char out[128]);
