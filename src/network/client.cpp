#include "pch.h"
#include "client.h"
#include "socket.h"
#include "core/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>


static network_socket clientSocket;
static network_address serverAddress;

bool startNetworkClient(const char* serverIP, uint32 serverPort, const network_message_callback& callback)
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

    serverAddress = address;
    clientSocket = socket;

    return true;
}
