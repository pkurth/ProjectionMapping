#include "pch.h"
#include "server.h"
#include "socket.h"
#include "core/log.h"

#include <ws2tcpip.h>
#include <winsock2.h>



struct udp_connection
{
	network_address address;
};


static network_socket serverSocket;
static std::vector<udp_connection> activeConnections;

bool startNetworkServer(uint32 port)
{
	network_socket socket;
	if (!socket.initialize(port, false))
	{
		return false;
	}

	serverSocket = socket;

	return true;
}

receive_result checkForServerMessages(char* buffer, uint32 size, uint32& outBytesReceived, network_address& outClientAddress, bool& outClientKnown)
{
	network_address clientAddress;
	uint32 bytesReceived = serverSocket.receive(clientAddress, buffer, size);

	if (bytesReceived != -1 && bytesReceived != 0)
	{
		bool addressKnown = false;
		for (auto& con : activeConnections)
		{
			if (con.address == clientAddress)
			{
				addressKnown = true;
				break;
			}
		}

		outClientAddress = clientAddress;
		outClientKnown = addressKnown;
		outBytesReceived = bytesReceived;

		if (!addressKnown)
		{
			activeConnections.push_back({ clientAddress });
		}

		return receive_result_success;
	}

	return receive_result_nothing_received;
}

bool sendTo(const network_address& address, const char* data, uint32 size)
{
	return serverSocket.send(address, data, size);
}

bool broadcastToClients(const char* data, uint32 size)
{
	bool result = true;

	for (auto& con : activeConnections)
	{
		result &= serverSocket.send(con.address, data, size);
	}

	return result;
}
