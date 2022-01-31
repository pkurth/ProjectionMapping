#include "pch.h"
#include "client.h"
#include "socket.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>


static network_socket clientSocket;
static network_address serverAddress;

bool startNetworkClient(const char* serverIP, uint32 serverPort)
{
    network_address serverAddress;
    if (!serverAddress.initialize(serverIP, serverPort))
    {
        return false;
    }

    network_socket socket;
    if (!socket.initialize(0, false))
    {
        return false;
    }

    ::serverAddress = serverAddress;
    clientSocket = socket;

    return true;
}

receive_result checkForClientMessages(char* buffer, uint32 size, uint32& outBytesReceived)
{
	network_address address;
	uint32 bytesReceived = clientSocket.receive(address, buffer, size);

	if (bytesReceived == -1)
	{
		return receive_result_connection_closed;
	}

	if (bytesReceived != 0)
	{
		if (address == ::serverAddress)
		{
			outBytesReceived = bytesReceived;
			return receive_result_success;
		}
	}

	return receive_result_nothing_received;
}

bool sendToServer(const char* data, uint32 size)
{
	return clientSocket.send(serverAddress, data, size);
}
