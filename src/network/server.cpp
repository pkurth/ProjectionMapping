#include "pch.h"
#include "server.h"
#include "core/log.h"

#include <ws2tcpip.h>
#include <winsock2.h>


#define RECEIVE_BUFFER_SIZE 512

network_server::network_server()
    : listenSocket(INVALID_SOCKET)
{
}

bool network_server::initialize(uint32 port)
{
    addrinfo* addrResult = 0,
        hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;


    char portString[16];
    snprintf(portString, sizeof(portString), "%u", port);

    int result = getaddrinfo(NULL, portString, &hints, &addrResult);
    if (result != 0) 
    {
        LOG_ERROR("Failed to get address info: %d\n", result);
        return false;
    }

    listenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);

    if (listenSocket == INVALID_SOCKET) 
    {
        LOG_ERROR("Failed to create socket: %d\n", WSAGetLastError());
        freeaddrinfo(addrResult);
        return false;
    }

    result = bind(listenSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen);
    if (result == SOCKET_ERROR) 
    {
        LOG_ERROR("Failed to bind socket: %d\n", WSAGetLastError());
        freeaddrinfo(addrResult);
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    this->port = port;



#if 1
	char ac[80];
	if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR) 
	{
		LOG_ERROR("Failed to get local host name");
		freeaddrinfo(addrResult);
		closesocket(listenSocket);
		listenSocket = INVALID_SOCKET;
		return false;
	}

	struct hostent* phe = gethostbyname(ac);
	if (phe == 0) 
	{
		LOG_ERROR("Failed to look up host name");
		freeaddrinfo(addrResult);
		closesocket(listenSocket);
		listenSocket = INVALID_SOCKET;
		return false;
	}

	if (!phe->h_addr_list[0])
	{
		LOG_ERROR("Failed to get address list");
		freeaddrinfo(addrResult);
		closesocket(listenSocket);
		listenSocket = INVALID_SOCKET;
		return false;
	}
	
	struct in_addr addr;
	memcpy(&addr, phe->h_addr_list[0], sizeof(struct in_addr));

	char serverAddress[64];
	inet_ntop(AF_INET, &addr, serverAddress, sizeof(serverAddress));
#else
	sockaddr_in* addrIn = (sockaddr_in*)addrResult->ai_addr;

	char serverAddress[64];
	inet_ntop(AF_INET, &addrIn, serverAddress, sizeof(serverAddress));
#endif

	LOG_MESSAGE("Server created, IP: %s, port %u", serverAddress, port);
	
	freeaddrinfo(addrResult);

    return true;
}

void network_server::shutdown()
{
    for (auto& con : connections)
    {
        closesocket(con.socket);
    }
    connections.clear();

    if (listenSocket != INVALID_SOCKET)
    {
        closesocket(listenSocket);
    }
    listenSocket = INVALID_SOCKET;

	serverThread.join();
}

bool network_server::run()
{
    if (listenSocket == INVALID_SOCKET)
    {
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) 
    {
        LOG_ERROR("Failed to listen on port %u: %d\n", port, WSAGetLastError());
        return false;
    }

   serverThread = std::thread([this]()
	{
		fd_set readfds = {};

		while (true)
		{
			FD_ZERO(&readfds);
			FD_SET(listenSocket, &readfds);

			for (auto& con : connections)
			{
				FD_SET(con.socket, &readfds);
			}


			int activity = select(0, &readfds, NULL, NULL, NULL);
			if (activity == SOCKET_ERROR)
			{
				LOG_ERROR("Failed to select : %d", WSAGetLastError());
				return false;
			}

			if (FD_ISSET(listenSocket, &readfds))
			{
				sockaddr_in address;
				int addrlen = (int)sizeof(address);

				SOCKET clientSocket = accept(listenSocket, (struct sockaddr*)&address, &addrlen);
				if (clientSocket == INVALID_SOCKET)
				{
					LOG_ERROR("Failed to accept connection: %d", WSAGetLastError());
					return false;
				}

				char clientAddress[64];
				inet_ntop(AF_INET, &address.sin_addr, clientAddress, sizeof(clientAddress));
				LOG_MESSAGE("New client connected, IP: %s, port %d", clientAddress, ntohs(address.sin_port));

				connections.push_back({ clientSocket });

				////send new connection greeting message
				//if (send(new_socket, message, strlen(message), 0) != strlen(message))
				//{
				//    perror("send failed");
				//}

				//puts("Welcome message sent successfully");
			}


			// Check for messages.
			for (uint32 i = 0; i < (uint32)connections.size(); ++i)
			{
				active_connection& con = connections[i];
				SOCKET s = con.socket;

				if (FD_ISSET(s, &readfds))
				{
					sockaddr_in address;
					int addrlen = (int)sizeof(address);

					// Get details of the client.
					getpeername(s, (struct sockaddr*)&address, (int*)&addrlen);


					char buffer[RECEIVE_BUFFER_SIZE];
					int bufferLength = RECEIVE_BUFFER_SIZE;

					//Check if it was for closing , and also read the incoming message
					//recv does not place a null terminator at the end of the string (whilst printf %s assumes there is one).
					int bytesReceived = recv(s, buffer, bufferLength, 0);

					if (bytesReceived == 0)
					{
						char clientAddress[64];
						inet_ntop(AF_INET, &address.sin_addr, clientAddress, sizeof(clientAddress));
						LOG_MESSAGE("Client disconnected, IP: %s, port %d", clientAddress, ntohs(address.sin_port));
						closesocket(s);
						connections[i] = connections.back();
						connections.pop_back();
						--i;
						continue;
					}

					if (bytesReceived == SOCKET_ERROR)
					{
						LOG_ERROR("Failed to receive data: %d\n", WSAGetLastError());
						closesocket(s);
						connections[i] = connections.back();
						connections.pop_back();
						--i;
						continue;
					}

					assert(bytesReceived > 0);

					LOG_MESSAGE("Bytes received: %d\n", bytesReceived);
					if (callback)
					{
						callback(buffer, bytesReceived);
					}

				}

			}
		}
	});
    

    return true;
}
