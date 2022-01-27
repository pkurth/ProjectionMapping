#include "pch.h"
#include "client.h"
#include "socket.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>


static network_socket clientSocket;
static network_address serverAddress;

bool startNetworkClient(const char* serverIP, uint32 serverPort, const client_message_callback& messageCallback, const client_close_callback& closeCallback)
{
    network_address serverAddress;
    if (!serverAddress.initialize(serverIP, serverPort))
    {
        return false;
    }

    network_socket socket;
    if (!socket.initialize(0))
    {
        return false;
    }

	char clientAddress[128];
	if (!getLocalIPAddress(clientAddress))
	{
		socket.close();
		return false;
	}


	std::thread thread([messageCallback, closeCallback]()
	{
		while (true)
		{
			char buffer[NETWORK_BUFFER_SIZE];

			network_address address;
			uint32 bytesReceived = clientSocket.receive(address, buffer, NETWORK_BUFFER_SIZE);

			if (bytesReceived == -1)
			{
				if (closeCallback)
				{
					closeCallback();
				}
				break;
			}

			if (bytesReceived != 0)
			{
				if (address == ::serverAddress)
				{
					if (messageCallback)
					{
						messageCallback(buffer, bytesReceived);
					}
				}
			}
		}

		clientSocket.close();
	});
	thread.detach();

	LOG_MESSAGE("Client created, IP: %s", clientAddress);

    ::serverAddress = serverAddress;
    clientSocket = socket;

    return true;
}

bool sendToServer(const char* data, uint32 size)
{
	return clientSocket.send(serverAddress, data, size);
}
