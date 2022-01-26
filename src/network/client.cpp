#include "pch.h"
#include "client.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>



network_client::network_client()
    : connectSocket(INVALID_SOCKET)
{
}

bool network_client::initialize(const char* addressToConnectTo, uint32 portToConnectTo)
{
    addrinfo* addrResult = 0,
        hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;// AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portString[16];
    snprintf(portString, sizeof(portString), "%u", portToConnectTo);

    int result = getaddrinfo(addressToConnectTo, portString, &hints, &addrResult);
    if (result != 0)
    {
        LOG_ERROR("Failed to get address info: %d\n", result);
        return false;
    }

    addrinfo* ptr = addrResult;

    // Create a Socket for connecting to server.
    connectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (connectSocket == INVALID_SOCKET)
    {
        LOG_ERROR("Failed to create socket: %d\n", WSAGetLastError());
        freeaddrinfo(addrResult);
        return false;
    }


    result = connect(connectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (result == SOCKET_ERROR) 
    {
        closesocket(connectSocket);
        connectSocket = INVALID_SOCKET;
        freeaddrinfo(addrResult);
        return false;
    }

    freeaddrinfo(addrResult);

    return true;
}

void network_client::shutdown()
{
    if (connectSocket != INVALID_SOCKET)
    {
        closesocket(connectSocket);
    }
    connectSocket = INVALID_SOCKET;
}

bool network_client::send(const void* data, uint32 size)
{
    if (connectSocket == INVALID_SOCKET)
    {
        return false;
    }

    int result = ::send(connectSocket, (const char*)data, size, 0);
    if (result == SOCKET_ERROR) 
    {
        LOG_ERROR("Failed to send: %d\n", WSAGetLastError());
        return false;
    }

    return true;
}
