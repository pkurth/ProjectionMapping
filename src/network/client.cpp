#include "pch.h"
#include "client.h"
#include "socket.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>


static network_socket clientSocket;
static network_address serverAddress;

bool startNetworkClient(const char* serverIP, uint32 serverPort, const client_message_callback& callback)
{
    network_address address;
    if (!address.initialize(serverIP, serverPort))
    {
        return false;
    }

    network_socket socket;
    if (!socket.initialize(0))
    {
        return false;
    }


	std::thread thread([callback]()
	{
		while (true)
		{
			char buffer[NETWORK_BUFFER_SIZE];

			network_address address;
			uint32 bytesReceived = clientSocket.receive(address, buffer, NETWORK_BUFFER_SIZE);

			if (bytesReceived != 0)
			{
				if (address == serverAddress)
				{
					if (callback)
					{
						callback(buffer, bytesReceived);
					}
				}
			}
		}
	});
	thread.detach();

	LOG_MESSAGE("Client created");

    serverAddress = address;
    clientSocket = socket;

    return true;
}

bool sendToServer(const char* data, uint32 size)
{
	return clientSocket.send(serverAddress, data, size);
}
