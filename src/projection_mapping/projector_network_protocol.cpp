#include "pch.h"
#include "projector_network_protocol.h"

#include "network/network.h"
#include "network/client.h"
#include "network/server.h"

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
static projector_solver* solver;
static projector_solver_settings oldSolverSettings;

static float timeSinceLastUpdate = 0.f;
static const float updateTime = 1.f / 30.f;

enum message_type : uint16
{
	message_hello_from_client,
	message_object_info,
	message_projector_info,
	message_tracking,
	message_solver_settings,
};

struct message_header
{
	message_type type;
	uint16 clientID;
	uint32 messageID; // Used to ignore out-of-order tracking info.
};

struct hostname_message
{
	char hostname[32];
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


struct message_buffer
{
	uint32 size = sizeof(message_header);

	template <typename T>
	T* push(uint32 count)
	{
		assert(size + count * sizeof(T) <= NETWORK_BUFFER_SIZE);
		T* result = (T*)(buffer + size);
		size += count * (uint32)sizeof(T);
		return result;
	}

	union
	{
		message_header header;
		char buffer[NETWORK_BUFFER_SIZE];
	};

};


static auto getObjectGroup()
{
	auto objectGroup = scene->group(entt::get<raster_component, transform_component>);
	return objectGroup;
}

// ----------------------------------------
// SERVER
// ----------------------------------------

namespace server
{
	static uint16 runningClientID = 0;
	static uint32 runningTrackingMessageID = 0;

	struct client_connection
	{
		std::string name;
		uint16 clientID;
		network_address address;
	};

	static std::vector<client_connection> clientConnections;
	static std::mutex mutex;


	static void sendObjectInformation(const client_connection& connection)
	{
		auto objectGroup = getObjectGroup();
		uint32 numObjectsInScene = (uint32)objectGroup.size();

		message_buffer messageBuffer;
		messageBuffer.header.type = message_object_info;
		messageBuffer.header.clientID = connection.clientID;
		messageBuffer.header.messageID = 0;

		object_message* messages = messageBuffer.push<object_message>(numObjectsInScene);

		uint32 id = 0;
		for (auto [entityHandle, raster, transform] : objectGroup.each())
		{
			object_message& msg = messages[id];

			std::string path = getPathFromAssetHandle(raster.mesh->handle).string();

			strncpy_s(msg.filename, path.c_str(), sizeof(msg.filename));
			memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
			memcpy(msg.position, transform.position.data, sizeof(vec3));
			memcpy(msg.scale, transform.scale.data, sizeof(vec3));
			msg.id = id;

			++id;
		}

		sendTo(connection.address, messageBuffer.buffer, messageBuffer.size);
	}

	// Do not call the following functions from callback!
	static void sendObjectInformation()
	{
		mutex.lock();
		for (const client_connection& connection : clientConnections)
		{
			sendObjectInformation(connection);
		}
		mutex.unlock();
	}

	static void sendTrackingInformation()
	{
		auto objectGroup = getObjectGroup();
		uint32 numObjectsInScene = (uint32)objectGroup.size();

		message_buffer messageBuffer;
		messageBuffer.header.type = message_tracking;
		messageBuffer.header.messageID = runningTrackingMessageID++;

		tracking_message* messages = messageBuffer.push<tracking_message>(numObjectsInScene);

		uint32 id = 0;
		for (auto [entityHandle, raster, transform] : objectGroup.each())
		{
			tracking_message& msg = messages[id];

			memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
			memcpy(msg.position, transform.position.data, sizeof(vec3));
			msg.id = id;

			++id;
		}


		mutex.lock();
		for (const client_connection& connection : clientConnections)
		{
			messageBuffer.header.clientID = connection.clientID;
			sendTo(connection.address, messageBuffer.buffer, messageBuffer.size);
		}
		mutex.unlock();
	}

	static void sendProjectorInformation()
	{

	}

	static void sendSolverSettings()
	{
		LOG_MESSAGE("Sending solver settings");

		message_buffer messageBuffer;
		messageBuffer.header.type = message_solver_settings;
		messageBuffer.header.messageID = 0;

		projector_solver_settings* message = messageBuffer.push<projector_solver_settings>(1);
		*message = solver->settings;

		mutex.lock();
		for (const client_connection& connection : clientConnections)
		{
			messageBuffer.header.clientID = connection.clientID;
			sendTo(connection.address, messageBuffer.buffer, messageBuffer.size);
		}
		mutex.unlock();
	}

	static void messageCallback(const char* data, uint32 size, const network_address& clientAddress, bool clientAlreadyKnown)
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

				if (size < sizeof(hostname_message))
				{
					LOG_ERROR("Message is smaller than sizeof(hostname_message). Expected at least %u bytes after header, got %u", (uint32)sizeof(hostname_message), size);
					return;
				}

				hostname_message* hostname = (hostname_message*)data;
				data = (const char*)(hostname + 1);
				size -= sizeof(hostname_message);

				LOG_MESSAGE("Received message identifies client as '%s'", hostname->hostname);

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


				uint16 clientID = runningClientID++;
				LOG_MESSAGE("Assigning new client ID %u", clientID);

				client_connection connection = { hostname->hostname, clientID, clientAddress };
				mutex.lock();
				clientConnections.push_back(connection);
				mutex.unlock();

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
	static uint32 latestTrackingMessageID = 0;

	static std::vector<scene_entity> trackedObjects;

	static void sendHello()
	{
		std::vector<monitor_info>& monitors = win32_window::allConnectedMonitors;

		message_buffer messageBuffer;
		messageBuffer.header.type = message_hello_from_client;
		messageBuffer.header.clientID = 0; // No client ID has been assigned yet.
		messageBuffer.header.messageID = 0;

		hostname_message* hostname = messageBuffer.push<hostname_message>(1);
		gethostname(hostname->hostname, sizeof(hostname->hostname));

		monitor_message* messages = messageBuffer.push<monitor_message>((uint32)monitors.size());

		for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
		{
			monitor_message& msg = messages[i];
			monitor_info& mon = monitors[i];

			strncpy_s(msg.name, mon.name.c_str(), sizeof(msg.name));
			strncpy_s(msg.uniqueID, mon.uniqueID.c_str(), sizeof(msg.uniqueID));
			msg.width = (uint16)mon.width;
			msg.height = (uint16)mon.height;
			msg.probablyProjector = mon.probablyProjector;
		}

		sendToServer(messageBuffer.buffer, messageBuffer.size);
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
			//projector_component::myComputerID = (int32)header->clientID;
		}

		if (clientID != header->clientID)
		{
			LOG_ERROR("Received message with non-matching client ID. Expected %u, got %u. Ignoring message", clientID, (uint32)header->clientID);
			return;
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

				// Delete objects in scene.
				auto objectGroup = getObjectGroup();
				scene->registry.destroy(objectGroup.begin(), objectGroup.end());
				trackedObjects.clear();

				// Populate with received objects.
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

			case message_tracking:
			{
				if (size % sizeof(tracking_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(tracking_message). Expected multiple of %u, got %u", (uint32)sizeof(tracking_message), size);
					return;
				}

				if (header->messageID < latestTrackingMessageID)
				{
					// Ignore out-of-order tracking messages.
					return;
				}

				latestTrackingMessageID = header->messageID;

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

			case message_solver_settings:
			{
				if (size != sizeof(projector_solver_settings))
				{
					LOG_ERROR("Message size does not equal sizeof(projector_solver_settings). Expected %u, got %u", (uint32)sizeof(projector_solver_settings), size);
					return;
				}

				LOG_MESSAGE("Updating solver settings");

				solver->settings = *(const projector_solver_settings*)data;
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

bool startProjectorNetworkProtocol(game_scene& scene, projector_solver& solver, bool isServer)
{
	bool result = false;

	::isServer = isServer;
	::scene = &scene;
	::solver = &solver;
	::oldSolverSettings = solver.settings;

	if (isServer)
	{
		result = startNetworkServer(SERVER_PORT, server::messageCallback);
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
		timeSinceLastUpdate += dt;

		if (timeSinceLastUpdate >= updateTime)
		{
			server::sendTrackingInformation();

			if (memcmp(&solver->settings, &oldSolverSettings, sizeof(oldSolverSettings)) != 0)
			{
				oldSolverSettings = solver->settings;
				server::sendSolverSettings();
			}
			
			timeSinceLastUpdate -= updateTime;
		}
	}

	return true;
}

bool notifyProjectorNetworkOnSceneLoad()
{
	if (!projectorNetworkInitialized)
	{
		return false;
	}

	if (isServer)
	{
		server::sendObjectInformation();
	}

	return true;
}
