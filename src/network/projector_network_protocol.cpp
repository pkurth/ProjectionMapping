#include "pch.h"
#include "projector_network_protocol.h"

#include "network.h"
#include "client.h"
#include "server.h"

#include "core/log.h"
#include "window/window.h"
#include "core/file_registry.h"

static const uint32 SERVER_PORT = 27015;

#if NETWORK_FAMILY == AF_INET
static const char* SERVER_IP = "131.188.49.110";
#else
static const char* SERVER_IP = "fe80::96b:37d3:ee41:b0a";
#endif


static bool isServer;
static game_scene* scene;
static float timeSinceLastPositionUpdate = 0.f;


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

struct object_message
{
	char filename[32];
	float rotation[4];
	float position[3];
	float scale[3];
	uint32 id;
};


#define ALLOCATE_ARRAY_MESSAGE(message_t, count) \
	uint32 size = (uint32)(sizeof(message_header) + sizeof(message_t) * count); \
	message_header* header = (message_header*)alloca(size); \
	message_t* message = (message_t*)(header + 1);


// ----------------------------------------
// SERVER
// ----------------------------------------

namespace server
{
	static uint32 runningClientID = 0;

	struct client_connection
	{
		uint32 id;
		network_address address;
	};

	static std::vector<client_connection> clientConnections;

	static void callback(const char* data, uint32 size, const network_address& clientAddress, bool clientAlreadyKnown)
	{
#ifndef TEST_CLIENT
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

				clientConnections.push_back({ clientID, clientAddress });

				// Response.
				{
					auto objectGroup = scene->group(entt::get<raster_component, transform_component>);
					uint32 numObjectsInScene = (uint32)objectGroup.size();

					ALLOCATE_ARRAY_MESSAGE(object_message, numObjectsInScene);

					header->type = message_hello_from_server;
					header->clientID = clientID;

					uint32 id = 0;
					for (auto [entityHandle, raster, transform] : objectGroup.each())
					{
						object_message& msg = message[id];

						std::string path = getPathFromAssetHandle(raster.mesh->handle).string();

						strncpy_s(msg.filename, path.c_str(), sizeof(msg.filename));
						memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
						memcpy(msg.position, transform.position.data, sizeof(vec3));
						memcpy(msg.scale, transform.scale.data, sizeof(vec3));
						msg.id = id;

						++id;
					}

					sendTo(clientAddress, (const char*)header, size);
				}
			} break;

			default:
			{
				LOG_ERROR("Received message which we don't understand: %u", (uint32)header->type);
			} break;
		}

#endif
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

		ALLOCATE_ARRAY_MESSAGE(monitor_message, monitors.size());

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

				if (size % sizeof(object_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(object_message). Expected multiple of %u, got %u", (uint32)sizeof(object_message), size);
					return;
				}

				LOG_MESSAGE("Received 'hello' from server. Our client ID is %u", header->clientID);
				clientID = header->clientID;


				uint32 numObjects = size / (uint32)sizeof(object_message);
				const object_message* objects = (object_message*)data;

				LOG_MESSAGE("Received message containing %u objects", numObjects);
				for (uint32 i = 0; i < numObjects; ++i)
				{
					LOG_MESSAGE("Object %u: %s", objects[i].id, objects[i].filename);
				}

			} break;

			default:
			{
				LOG_ERROR("Received message which we don't understand: %u", (uint32)header->type);
			} break;
		}
	}
}

bool startProjectorNetworkProtocol(game_scene& scene, bool isServer)
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
	::scene = &scene;
	return result;
}

bool updateProjectorNetworkProtocol(float dt)
{
#ifndef TEST_CLIENT
	if (isServer)
	{
		for (auto [entityHandle, raster, transform] : scene->group(entt::get<raster_component, transform_component>).each())
		{

		}
	}
#endif

	return true;
}
