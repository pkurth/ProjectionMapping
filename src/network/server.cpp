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
static std::mutex mutex;

bool startNetworkServer(uint32 port, const network_message_callback& callback)
{
	network_socket socket;
	if (!socket.initialize(port))
	{
		return false;
	}

	char serverAddress[128];
	if (!getLocalIPAddress(serverAddress))
	{
		socket.close();
		return false;
	}

	serverSocket = socket;


	std::thread thread([callback]()
	{
		while (true)
		{
			char buffer[NETWORK_BUFFER_SIZE];

			network_address clientAddress;
			uint32 bytesReceived = serverSocket.receive(clientAddress, buffer, NETWORK_BUFFER_SIZE);

			if (bytesReceived != 0)
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

				if (!addressKnown)
				{
					mutex.lock();
					activeConnections.push_back({ clientAddress });
					mutex.unlock();
				}

				if (callback)
				{
					callback(buffer, bytesReceived);
				}
			}
		}
	});
	thread.detach();

	LOG_MESSAGE("Server created, IP: %s, port %u", serverAddress, port);

	return true;
}

bool broadcastToClients(const char* data, uint32 size)
{
	bool result = true;

	mutex.lock();
	for (auto& con : activeConnections)
	{
		result &= serverSocket.send(con.address, data, size);
	}
	mutex.unlock();

	return result;
}
