#pragma once

// AF_INET  for IPv4
// AF_INET6 for IPv6
#define NETWORK_FAMILY AF_INET


enum receive_result
{
	receive_result_success,
	receive_result_nothing_received,
	receive_result_connection_closed,
};

bool initializeNetwork();
void shutdownNetwork();

bool getLocalIPAddress(char out[128]);
