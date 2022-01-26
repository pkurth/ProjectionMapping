#include "pch.h"
#include "server.h"
#include "socket.h"
#include "core/log.h"

#include <ws2tcpip.h>
#include <winsock2.h>


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

	LOG_MESSAGE("Server created, IP: %s, port %u", serverAddress, port);


#if 0
	{
		// SEND CODE.

		network_address clientAddress;
		if (clientAddress.initialize(clientIP, clientPort))
		{
			//socket.send(c)
		}


	}

	{
		// RECEIVE CODE.

		while (true)
		{
			char buffer[NETWORK_BUFFER_SIZE];

			network_address clientAddress;
			uint32 bytesReceived = socket.receive(clientAddress, buffer, NETWORK_BUFFER_SIZE);

			if (bytesReceived == 0)
			{
				break;
			}
		}
	}
#endif

	/*std::thread serverThread([]()
	{
		while (true)
		{
			char buffer[NETWORK_BUFFER_SIZE];

			network_address clientAddress;
			uint32 bytesReceived = socket.receive(clientAddress, buffer, NETWORK_BUFFER_SIZE);

			if (bytesReceived == 0)
			{
				break;
			}
		}
	});*/

	return true;
}
