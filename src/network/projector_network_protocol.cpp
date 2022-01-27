#include "pch.h"
#include "projector_network_protocol.h"

#include "network.h"
#include "client.h"
#include "server.h"

#include "core/log.h"
#include "window/window.h"
#include "core/file_registry.h"


#if NETWORK_FAMILY == AF_INET
char SERVER_IP[128] = "131.188.49.110";
#else
char SERVER_IP[128] = "fe80::96b:37d3:ee41:b0a";
#endif

uint32 SERVER_PORT = 27015;

bool projectorNetworkInitialized = false;
static bool isServer;
static game_scene* scene;
static float timeSinceLastPositionUpdate = 0.f;
static const float trackingUpdateTime = 1.f / 30.f;

enum message_type
{
	message_hello_from_client,
	message_object_info,
	message_projector_info,
	message_tracking_info,
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

struct tracking_message
{
	float rotation[4];
	float position[3];
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

	// TODO: Protect this with mutex!
	static std::vector<client_connection> clientConnections;


	static void sendObjectInformation(const client_connection& connection)
	{
#ifndef TEST_CLIENT
		auto objectGroup = scene->group(entt::get<raster_component, transform_component>);
		uint32 numObjectsInScene = (uint32)objectGroup.size();

		ALLOCATE_ARRAY_MESSAGE(object_message, numObjectsInScene);

		header->type = message_object_info;
		header->clientID = connection.id;

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

		sendTo(connection.address, (const char*)header, size);
#endif
	}

	static void sendTrackingInformation()
	{
#ifndef TEST_CLIENT
		auto objectGroup = scene->group(entt::get<raster_component, transform_component>);
		uint32 numObjectsInScene = (uint32)objectGroup.size();

		ALLOCATE_ARRAY_MESSAGE(tracking_message, numObjectsInScene);

		uint32 id = 0;
		for (auto [entityHandle, raster, transform] : objectGroup.each())
		{
			tracking_message& msg = message[id];

			memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
			memcpy(msg.position, transform.position.data, sizeof(vec3));
			msg.id = id;

			++id;
		}

		for (const client_connection& connection : clientConnections)
		{
			header->type = message_tracking_info;
			header->clientID = connection.id;

			sendTo(connection.address, (const char*)header, size);
		}
#endif
	}

	static void sendProjectorInformation()
	{

	}

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

				client_connection connection = clientConnections.emplace_back(client_connection{ clientID, clientAddress });

				// Response.
				sendObjectInformation(connection);
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
	static uint32 clientID = -1;

	static std::vector<scene_entity> trackedObjects;

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

	static void messageCallback(const char* data, uint32 size)
	{
		if (size < sizeof(message_header))
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), size);
			return;
		}

		const message_header* header = (const message_header*)data;
		data = (const char*)(header + 1);
		size -= sizeof(message_header);

		if (clientID == -1)
		{
			LOG_MESSAGE("Received first message from server. Assigning client ID %u", header->clientID);
			clientID = header->clientID;
		}

		if (clientID != header->clientID)
		{
			LOG_ERROR("Received message with non-matching client ID. Expected %u, got %u", clientID, header->clientID);
		}

		switch (header->type)
		{
			case message_object_info:
			{
				if (size % sizeof(object_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(object_message). Expected multiple of %u, got %u", (uint32)sizeof(object_message), size);
					return;
				}

				uint32 numObjects = size / (uint32)sizeof(object_message);
				const object_message* objects = (object_message*)data;

				LOG_MESSAGE("Received message containing %u objects", numObjects);
				for (uint32 i = 0; i < numObjects; ++i)
				{
					LOG_MESSAGE("Object %u: %s", objects[i].id, objects[i].filename);

					if (auto targetObjectMesh = loadMeshFromFile(objects[i].filename))
					{
						trs transform;
						memcpy(transform.rotation.v4.data, objects[i].rotation, sizeof(quat));
						memcpy(transform.position.data, objects[i].position, sizeof(vec3));
						memcpy(transform.scale.data, objects[i].scale, sizeof(vec3));

						auto targetObject = scene->createEntity("Target object")
							.addComponent<transform_component>(transform)
							.addComponent<raster_component>(targetObjectMesh);

						trackedObjects.push_back(targetObject);
					}
				}

			} break;

			case message_tracking_info:
			{
				if (size % sizeof(tracking_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(tracking_message). Expected multiple of %u, got %u", (uint32)sizeof(tracking_message), size);
					return;
				}

				uint32 numObjects = size / (uint32)sizeof(tracking_message);
				const tracking_message* objects = (tracking_message*)data;

				//LOG_MESSAGE("Received message containing %u objects", numObjects);

				for (uint32 i = 0; i < numObjects; ++i)
				{
					if (i < trackedObjects.size())
					{
						scene_entity o = trackedObjects[i];

						transform_component& transform = o.getComponent<transform_component>();
						memcpy(transform.rotation.v4.data, objects[i].rotation, sizeof(quat));
						memcpy(transform.position.data, objects[i].position, sizeof(vec3));
					}
				}

			} break;

			default:
			{
				LOG_ERROR("Received message which we don't understand: %u", (uint32)header->type);
			} break;
		}
	}

	static void closeCallback()
	{
		LOG_MESSAGE("Connection closed");
		projectorNetworkInitialized = false;
	}
}

bool startProjectorNetworkProtocol(game_scene& scene, bool isServer)
{
	bool result = false;

	if (isServer)
	{
		result = startNetworkServer(SERVER_PORT, server::callback);
		getLocalIPAddress(SERVER_IP);
	}
	else
	{
		result = startNetworkClient(SERVER_IP, SERVER_PORT, client::messageCallback, client::closeCallback);
		
		if (result)
		{
			client::sendHello();
		}
	}

	::isServer = isServer;
	::scene = &scene;

	projectorNetworkInitialized = true;

	return result;
}

bool updateProjectorNetworkProtocol(float dt)
{
	if (!projectorNetworkInitialized)
	{
		return false;
	}

	if (isServer)
	{
		timeSinceLastPositionUpdate += dt;

		if (timeSinceLastPositionUpdate >= trackingUpdateTime)
		{
			server::sendTrackingInformation();
			
			timeSinceLastPositionUpdate -= trackingUpdateTime;
		}
	}

	return true;
}
