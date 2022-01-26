#include "pch.h"
#include "server.h"
#include "core/log.h"

#include <ws2tcpip.h>
#include <winsock2.h>


struct active_connection
{
	SOCKET socket;
};

static std::vector<active_connection> connections;
static std::mutex mutex;

bool startNetworkServer(uint32 port, const network_message_callback& callback)
{
	SOCKET listenSocket = socket(NETWORK_FAMILY, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
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

	if (bind(listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		LOG_ERROR("Failed to bind socket: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		return false;
	}




	char ac[80];
	if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR)
	{
		LOG_ERROR("Failed to get local host name");
		closesocket(listenSocket);
		return false;
	}

	addrinfo* addrInfo;
	int err = getaddrinfo(ac, 0, 0, &addrInfo);
	if (err != 0)
	{
		LOG_ERROR("Failed to get address info: %s\n", gai_strerrorA(err));
		closesocket(listenSocket);
		return false;
	}

	char serverAddress[128];
	bool addressFound = false;

	for (addrinfo* p = addrInfo; p != 0; p = p->ai_next)
	{
		in_addr* addr;
		if (p->ai_family == NETWORK_FAMILY)
		{
			if (p->ai_family == AF_INET)
			{
				sockaddr_in* ipv = (sockaddr_in*)p->ai_addr;
				addr = &(ipv->sin_addr);
			}
			else
			{
				struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
				addr = (in_addr*)&(ipv6->sin6_addr);
			}

			inet_ntop(p->ai_family, addr, serverAddress, sizeof(serverAddress));

			addressFound = true;
			break;
		}
	}

	if (!addressFound)
	{
		LOG_ERROR("Failed to retrieve own address");
		closesocket(listenSocket);
		return false;
	}

	freeaddrinfo(addrInfo);


	LOG_MESSAGE("Server created, IP: %s, port %u", serverAddress, port);



	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		LOG_ERROR("Failed to listen on port %u: %d\n", port, WSAGetLastError());
		return false;
	}

	std::thread serverThread = std::thread([listenSocket, callback]()
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
#if NETWORK_FAMILY == AF_INET
					sockaddr_in address;
#else
					sockaddr_in6 address;
#endif
					int addrlen = (int)sizeof(address);

					SOCKET clientSocket = accept(listenSocket, (struct sockaddr*)&address, &addrlen);
					if (clientSocket == INVALID_SOCKET)
					{
						LOG_ERROR("Failed to accept connection: %d", WSAGetLastError());
						return false;
					}

#if NETWORK_FAMILY == AF_INET
					const void* sinAddr = &address.sin_addr;
					uint32 port = ntohs(address.sin_port);
#else
					const void* sinAddr = &address.sin6_addr;
					uint32 port = ntohs(address.sin6_port);
#endif


					char clientAddress[128];
					inet_ntop(NETWORK_FAMILY, sinAddr, clientAddress, sizeof(clientAddress));
					LOG_MESSAGE("New client connected, IP: %s, port %d", clientAddress, port);

					mutex.lock();
					connections.push_back({ clientSocket });
					mutex.unlock();

					////send new connection greeting message
					//if (send(new_socket, message, strlen(message), 0) != strlen(message))
					//{
					//    perror("send failed");
					//}

					//puts("Welcome message sent successfully");
				}
				else
				{
					// Check for messages.
					for (uint32 i = 0; i < (uint32)connections.size(); ++i)
					{
						active_connection& con = connections[i];
						SOCKET s = con.socket;

						if (FD_ISSET(s, &readfds))
						{
							char buffer[NETWORK_BUFFER_SIZE];

							int bytesReceived = recv(s, buffer, NETWORK_BUFFER_SIZE, 0);

							if (bytesReceived <= 0)
							{
								LOG_MESSAGE("Client disconnected");

								mutex.lock();
								closesocket(s);
								connections[i] = connections.back();
								connections.pop_back();
								mutex.unlock();

								--i;
								continue;
							}

							LOG_MESSAGE("Bytes received: %d\n", bytesReceived);

							if (callback)
							{
								callback(buffer, bytesReceived);
							}

						}

					}
				}
			}

			LOG_MESSAGE("Closing server");
		});

	serverThread.detach();

	return true;
}

bool broadcastMessageToClients(const void* data, uint64 size)
{
	bool result = true;

	mutex.lock();

	LOG_MESSAGE("Broadcasting message to %u clients", (uint32)connections.size());

	for (uint32 i = 0; i < (uint32)connections.size(); ++i)
	{
		active_connection& con = connections[i];
		SOCKET s = con.socket;

		int result = send(s, (const char*)data, (uint32)size, 0);
		if (result == SOCKET_ERROR)
		{
			LOG_ERROR("Failed to send: %d\n", WSAGetLastError());
			result = false;
		}
	}
	mutex.unlock();

	return result;
}
