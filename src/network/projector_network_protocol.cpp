#include "pch.h"
#include "projector_network_protocol.h"

#include "network.h"
#include "client.h"
#include "server.h"

#include "core/log.h"
#include "window/window.h"

static const uint32 SERVER_PORT = 27015;

#if NETWORK_FAMILY == AF_INET
static const char* SERVER_IP = "131.188.49.110";
#else
static const char* SERVER_IP = "fe80::96b:37d3:ee41:b0a";
#endif


enum message_type
{
	message_hello_from_client,
	message_hello_from_server,
};

struct message_header
{
	message_type type;
	uint32 clientID;
};

struct monitor_message
{
	char name[32];
	char uniqueID[128];
	uint16 width;
	uint16 height;
	bool probablyProjector;
};



// ----------------------------------------
// SERVER
// ----------------------------------------

namespace server
{
	static uint32 runningClientID = 1;

	static void callback(const char* data, uint32 size, const network_address& clientAddress, bool clientAlreadyKnown)
	{
		if (size < sizeof(message_header))
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), size);
			return;
		}

		const message_header* header = (const message_header*)data;
		data = (const char*)(header + 1);
		size -= sizeof(message_header);

		switch (header->type)
		{
			case message_hello_from_client:
			{
				if (clientAlreadyKnown)
				{
					LOG_MESSAGE("Received duplicate 'hello' from client. Ignoring message");
					return;
				}

				if (size % sizeof(monitor_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(monitor_message). Expected multiple of %u, got %u", (uint32)sizeof(monitor_message), size);
					return;
				}

				uint32 numMonitors = size / (uint32)sizeof(monitor_message);
				const monitor_message* monitors = (monitor_message*)data;

				LOG_MESSAGE("Received message containing %u monitors", numMonitors);
				for (uint32 i = 0; i < numMonitors; ++i)
				{
					LOG_MESSAGE("Monitor %u: %s (%ux%u pixels)", i, monitors[i].name, monitors[i].width, monitors[i].height);
				}

				uint32 clientID = runningClientID++;

				LOG_MESSAGE("Assigning new client ID %u", clientID);

				message_header response;
				response.type = message_hello_from_server;
				response.clientID = clientID;

				sendTo(clientAddress, (const char*)&response, sizeof(message_header));
			} break;

			default:
			{
				LOG_ERROR("Received message which we don't understand: %u", (uint32)header->type);
			} break;
		}

	}
}



// ----------------------------------------
// CLIENT
// ----------------------------------------

namespace client
{
	static uint32 clientID = 0;

	static void sendHello()
	{
		std::vector<monitor_info>& monitors = win32_window::allConnectedMonitors;

		uint32 size = (uint32)(sizeof(message_header) + sizeof(monitor_message) * monitors.size());

		message_header* header = (message_header*)alloca(size);
		monitor_message* message = (monitor_message*)(header + 1);

		header->type = message_hello_from_client;
		header->clientID = 0; // No client ID has been assigned yet.

		for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
		{
			monitor_message& msg = message[i];
			monitor_info& mon = monitors[i];

			strncpy_s(msg.name, mon.name.c_str(), sizeof(msg.name));
			strncpy_s(msg.uniqueID, mon.uniqueID.c_str(), sizeof(msg.uniqueID));
			msg.width = (uint16)mon.width;
			msg.height = (uint16)mon.height;
			msg.probablyProjector = mon.probablyProjector;
		}

		sendToServer((const char*)header, size);
	}

	static void callback(const char* data, uint32 size)
	{
		if (size < sizeof(message_header))
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), size);
			return;
		}

		const message_header* header = (const message_header*)data;
		data = (const char*)(header + 1);
		size -= sizeof(message_header);

		switch (header->type)
		{
			case message_hello_from_server:
			{
				if (clientID != 0)
				{
					LOG_MESSAGE("Received duplicate 'hello' from server. Ignoring message");
					if (clientID != header->clientID)
					{
						LOG_ERROR("Duplicate 'hello' assigns us a different client ID. This should not happen!");
					}
					return;
				}
				LOG_MESSAGE("Received 'hello' from server. Our client ID is %u", header->clientID);
				clientID = header->clientID;
			} break;

			default:
			{
				LOG_ERROR("Received message which we don't understand: %u", (uint32)header->type);
			} break;
		}
	}
}


static bool isServer;

bool startProjectorNetworkProtocol(bool isServer)
{
	bool result = false;

	if (isServer)
	{
		result = startNetworkServer(SERVER_PORT, server::callback);
	}
	else
	{
		result = startNetworkClient(SERVER_IP, SERVER_PORT, client::callback);
		
		if (result)
		{
			client::sendHello();
		}
	}

	::isServer = isServer;
	return result;
}

bool updateProjectorNetworkProtocol()
{


	return true;
}
