#include "pch.h"
#include "socket.h"
#include "core/log.h"


bool network_socket::initialize(uint32 port, bool blocking)
{
	SOCKET s = ::socket(NETWORK_FAMILY, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
	{
		LOG_ERROR("Failed to create socket: %d\n", WSAGetLastError());
		return false;
	}

#if NETWORK_FAMILY == AF_INET
	sockaddr_in addr = {};
	addr.sin_family = NETWORK_FAMILY;
	addr.sin_port = htons(port); // Convert to network order.
#else
	sockaddr_in6 addr = {};
	addr.sin6_family = NETWORK_FAMILY;
	addr.sin6_port = htons(port); // Convert to network order.
#endif

	if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		LOG_ERROR("Failed to bind socket: %d\n", WSAGetLastError());
		closesocket(s);
		return false;
	}


	if (!blocking)
	{
		DWORD nonBlocking = 1;
		if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
		{
			LOG_ERROR("Failed to set socket to non-blocking");
			closesocket(s);
			return false;
		}
	}

	this->socket = s;

	return true;
}

void network_socket::close()
{
	if (socket != INVALID_SOCKET)
	{
		closesocket(socket);
		socket = INVALID_SOCKET;
	}
}

bool network_socket::isOpen()
{
	return socket != INVALID_SOCKET;
}

bool network_socket::send(const network_address& destination, const void* data, uint32 size)
{
	int sentBytes = sendto(socket, (const char*)data, size, 0, (sockaddr*)&destination.addr, (int)sizeof(destination.addr));

	if (sentBytes != size)
	{
		LOG_ERROR("Failed to send packet");
		return false;
	}

	return true;
}

receive_result network_socket::receive(network_address& sender, void* data, uint32 maxSize, uint32& outBytesReceived)
{
	int fromLength = (int)sizeof(sender.addr);
	int bytesReceived = recvfrom(socket, (char*)data, maxSize, 0, (sockaddr*)&sender.addr, &fromLength);

	receive_result result = receive_result_success;

	if (bytesReceived == SOCKET_ERROR)
	{
		int error = WSAGetLastError();

		result = receive_result_connection_closed;
		if (error == WSAEWOULDBLOCK || error == EWOULDBLOCK)
		{
			bytesReceived = 0;
			result = receive_result_nothing_received;
		}
	}

	outBytesReceived = (uint32)bytesReceived;
	return result;
}
