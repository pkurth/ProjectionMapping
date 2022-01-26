#include "pch.h"
#include "client.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>


static SOCKET connectSocket = INVALID_SOCKET;

bool startNetworkClient(const char* addressToConnectTo, uint32 portToConnectTo, const network_message_callback& callback)
{
    SOCKET connectSocket = socket(NETWORK_FAMILY, SOCK_STREAM, IPPROTO_TCP);
    if (connectSocket == INVALID_SOCKET)
    {
        LOG_ERROR("Failed to create socket: %d\n", WSAGetLastError());
        return false;
    }

#if NETWORK_FAMILY == AF_INET
    sockaddr_in addr = {};
    addr.sin_family = NETWORK_FAMILY;
    addr.sin_port = htons(portToConnectTo); // Convert to network order.
    if (inet_pton(NETWORK_FAMILY, addressToConnectTo, &addr.sin_addr) != 1)
    {
        LOG_ERROR("Failed to convert address: %d\n", WSAGetLastError());
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
        return false;
    }
#else
    sockaddr_in6 addr = {};
    addr.sin6_family = NETWORK_FAMILY;
    addr.sin6_port = htons(portToConnectTo); // Convert to network order.
    if (inet_pton(NETWORK_FAMILY, addressToConnectTo, &addr.sin6_addr) != 1)
    {
        LOG_ERROR("Failed to convert address: %d\n", WSAGetLastError());
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
        return false;
    }
#endif

    if (connect(connectSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        LOG_ERROR("Failed to connect: %d\n", WSAGetLastError());
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
        return false;
    }

    ::connectSocket = connectSocket;


    std::thread clientThread = std::thread([connectSocket, callback]()
        {
            char buffer[NETWORK_BUFFER_SIZE];

            while (true)
            {
                int bytesReceived = recv(connectSocket, buffer, NETWORK_BUFFER_SIZE, 0);

                if (bytesReceived <= 0)
                {
                    LOG_MESSAGE("Server disconnected");
                    break;
                }

                LOG_MESSAGE("Bytes received: %d\n", bytesReceived);

                if (callback)
                {
                    callback(buffer, bytesReceived);
                }
            } 
        });

    clientThread.detach();

    return true;
}

bool sendMessageToServer(const void* data, uint64 size)
{
    if (connectSocket == INVALID_SOCKET)
    {
        return false;
    }

    int result = send(connectSocket, (const char*)data, (uint32)size, 0);
    if (result == SOCKET_ERROR)
    {
        LOG_ERROR("Failed to send: %d\n", WSAGetLastError());
        return false;
    }

    LOG_ERROR("Sent %d bytes", result);

    return true;
}
