#include "pch.h"
#include "projector_network_protocol.h"
#include "projector_manager.h"
#include "tracking/tracking.h"

#include "network/socket.h"

#include "core/log.h"
#include "window/window.h"
#include "core/file_registry.h"


enum message_type : uint16
{
	message_client_hello,
	message_client_request_calibration_mode,
	message_client_local_calibration,

	message_server_solver_settings,
	message_server_object_info,
	message_server_object_update,
	message_server_projector_instantiation,
};

struct message_header
{
	message_type type;
	uint16 clientID;
	uint32 messageID; // Used to ignore out-of-order messages.
};

#define NETWORK_BUFFER_SIZE 4096

struct send_buffer
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

	template <typename T>
	bool pushValue(const T& t)
	{
		T* result = push<T>(1);
		if (result)
		{
			*result = t;
		}
		return result != 0;
	}

	union
	{
		message_header header;
		char buffer[NETWORK_BUFFER_SIZE];
	};
};

struct receive_buffer
{
	uint32 offset = 0;
	uint32 sizeRemaining = 0;
	uint32 actualSize;

	void reset()
	{
		offset = 0;
		sizeRemaining = actualSize;
	}

	template <typename T>
	T* get(uint32 count = 1)
	{
		uint32 s = (uint32)sizeof(T) * count;
		if (offset + s > actualSize)
		{
			return 0;
		}
		T* result = (T*)(buffer + offset);
		offset += s;
		sizeRemaining -= s;
		return result;
	}

	char buffer[NETWORK_BUFFER_SIZE];
};


struct client_hello_message
{
	char hostname[32];
};

struct client_monitor_info
{
	char description[64];
	char uniqueID[128];
};

struct server_object_message
{
	char filename[32];
	float rotation[4];
	float position[3];
	float scale[3];
	uint32 id;
	bool tracked;
};

struct server_object_update_message
{
	float rotation[4];
	float position[3];
	uint32 id;
};



bool projector_network_protocol::start(game_scene& scene, projector_manager* manager, bool isServer)
{
	bool result = false;

	this->isServer = isServer;
	this->scene = &scene;
	this->manager = manager;

	if (isServer)
	{
		result = server.initialize(scene, manager, serverPort, serverIP);
	}
	else
	{
		result = client.initialize(scene, manager, serverIP, serverPort);
	}

	if (result)
	{
		initialized = true;
	}

	return result;
}

bool projector_network_protocol::update(float dt)
{
	if (!initialized)
	{
		return false;
	}

	if (isServer)
	{
		return server.update(dt);
	}
	else
	{
		return client.update();
	}
}

bool projector_network_protocol::server_broadcastObjectInfo()
{
	if (!initialized)
	{
		return false;
	}

	if (isServer)
	{
		return server.broadcastObjectInfo();
	}

	return false;
}

bool projector_network_protocol::server_broadcastProjectors(const std::vector<projector_instantiation>& instantiations)
{
	if (!initialized)
	{
		return false;
	}

	if (isServer)
	{
		return server.broadcastProjectors(instantiations);
	}

	return false;
}

bool projector_network_protocol::client_reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calibs)
{
	if (!initialized)
	{
		return false;
	}

	if (!isServer)
	{
		return client.reportLocalCalibration(calibs);
	}

	return false;
}

uint32 projector_network_protocol::client_getID()
{
	if (!initialized || isServer)
	{
		return -1;
	}

	return client.clientID;
}

bool projector_network_server::initialize(game_scene& scene, projector_manager* manager, uint32 port, char* outIP)
{
	network_socket socket;
	if (!socket.initialize(port, false))
	{
		return false;
	}

	this->serverSocket = socket;
	this->manager = manager;
	this->scene = &scene;
	this->oldSolverSettings = manager->solver.settings;

	getLocalIPAddress(outIP);
	LOG_MESSAGE("Server created, IP: %s, port %u", outIP, port);

	return true;
}

bool projector_network_client::initialize(game_scene& scene, projector_manager* manager, const char* serverIP, uint32 serverPort)
{
	network_address serverAddr;
	if (!serverAddr.initialize(serverIP, serverPort))
	{
		return false;
	}

	network_socket socket;
	if (!socket.initialize(0, false))
	{
		return false;
	}

	this->serverAddress = serverAddr;
	this->clientSocket = socket;
	this->manager = manager;
	this->scene = &scene;
	this->oldSolverSettings = manager->solver.settings;
	connected = true;

	char clientAddress[128];
	getLocalIPAddress(clientAddress);
	LOG_MESSAGE("Client created, IP: %s", clientAddress);

	return sendHello();
}




bool projector_network_server::update(float dt)
{
	timeSinceLastUpdate += dt;

	if (timeSinceLastUpdate >= updateTime)
	{
		if (memcmp(&manager->solver.settings, &oldSolverSettings, sizeof(oldSolverSettings)) != 0)
		{
			oldSolverSettings = manager->solver.settings;
			
			LOG_MESSAGE("Sending solver settings");

			send_buffer messageBuffer;
			if (createSettingsMessage(messageBuffer))
			{
				sendToAllClients(messageBuffer);
			}
		}

		{
			send_buffer messageBuffer;
			if (createObjectUpdateMessage(messageBuffer))
			{
				sendToAllClients(messageBuffer);
			}
		}

		timeSinceLastUpdate -= updateTime;
	}




	receive_buffer messageBuffer;
	network_address clientAddress;

	while (serverSocket.receive(clientAddress, messageBuffer.buffer, sizeof(messageBuffer.buffer), messageBuffer.actualSize) == receive_result_success)
	{
		messageBuffer.reset();

		const message_header* header = messageBuffer.get<message_header>();
		if (!header)
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), messageBuffer.sizeRemaining);
			continue;
		}

		switch (header->type)
		{
			case message_client_hello:
			{
				bool addressKnown = std::find_if(clientConnections.begin(), clientConnections.end(), [&](const client_connection& c) { return c.address == clientAddress; }) != clientConnections.end();
				if (addressKnown)
				{
					LOG_MESSAGE("Received duplicate 'hello' from client. Ignoring message");
					break;
				}

				client_hello_message* msg = messageBuffer.get<client_hello_message>();
				if (!msg)
				{
					LOG_ERROR("Message is smaller than sizeof(hello_from_client_message). Expected at least %u bytes after header, got %u", (uint32)sizeof(client_hello_message), messageBuffer.sizeRemaining);
					break;
				}
				LOG_MESSAGE("Received message identifies client as '%s'", msg->hostname);

				if (messageBuffer.sizeRemaining % sizeof(client_monitor_info) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(client_monitor_info). Expected multiple of %u, got %u", (uint32)sizeof(client_monitor_info), messageBuffer.sizeRemaining);
					break;
				}

				uint32 numMonitors = messageBuffer.sizeRemaining / (uint32)sizeof(client_monitor_info);
				const client_monitor_info* monitors = messageBuffer.get<client_monitor_info>(numMonitors);

				assert(messageBuffer.sizeRemaining == 0);


				uint16 clientID = runningClientID++;
				LOG_MESSAGE("Assigning new client ID %u", clientID);

				client_connection connection = { clientID, clientAddress };
				clientConnections.push_back(connection);

				std::vector<std::string> descriptions;
				std::vector<std::string> uniqueIDs;
				for (uint32 i = 0; i < numMonitors; ++i)
				{
					descriptions.push_back(monitors[i].description);
					uniqueIDs.push_back(monitors[i].uniqueID);
				}

				{
					send_buffer response;
					if (createObjectMessage(response))
					{
						sendToClient(response, connection);
					}
				}

				{
					send_buffer response;
					if (createSettingsMessage(response))
					{
						sendToClient(response, connection);
					}
				}


				manager->network_newClient(msg->hostname, clientID, descriptions, uniqueIDs);


			} break;

			case message_client_request_calibration_mode:
			{
				if (messageBuffer.sizeRemaining != sizeof(projector_mode))
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(client_monitor_info). Expected multiple of %u, got %u", (uint32)sizeof(client_monitor_info), messageBuffer.sizeRemaining);
					break;
				}

				manager->solver.settings.mode = *messageBuffer.get<projector_mode>();

				if (manager->solver.settings.mode == projector_mode_calibration)
				{
					LOG_MESSAGE("Client %u requested calibration mode", (uint32)header->clientID);
				}
				else
				{
					LOG_MESSAGE("Client %u requested projection mapping mode", (uint32)header->clientID);
				}
			} break;
		}
	}

	return true;
}

bool projector_network_server::broadcastObjectInfo()
{
	send_buffer messageBuffer;
	return createObjectMessage(messageBuffer) && sendToAllClients(messageBuffer);
}

bool projector_network_server::broadcastProjectors(const std::vector<projector_instantiation>& instantiations)
{
	send_buffer messageBuffer;
	return createProjectorInstantiationMessage(messageBuffer, instantiations) && sendToAllClients(messageBuffer);
}

static auto getObjectGroup(game_scene* scene)
{
	auto objectGroup = scene->group(entt::get<raster_component, transform_component>);
	return objectGroup;
}


bool projector_network_server::createObjectMessage(send_buffer& buffer)
{
	auto objectGroup = getObjectGroup(scene);
	uint32 numObjectsInScene = (uint32)objectGroup.size();

	buffer.header.type = message_server_object_info;

	server_object_message* messages = buffer.push<server_object_message>(numObjectsInScene);
	if (!messages)
	{
		LOG_ERROR("Not enough space in message buffer to fit %u object infos", numObjectsInScene);
		return false;
	}

	uint32 id = 0;
	for (auto [entityHandle, raster, transform] : objectGroup.each())
	{
		scene_entity entity = { entityHandle, *scene };

		server_object_message& msg = messages[id];

		std::string path = getPathFromAssetHandle(raster.mesh->handle).string();

		strncpy_s(msg.filename, path.c_str(), sizeof(msg.filename));
		memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
		memcpy(msg.position, transform.position.data, sizeof(vec3));
		memcpy(msg.scale, transform.scale.data, sizeof(vec3));
		msg.id = (uint32)entityHandle;
		msg.tracked = entity.hasComponent<tracking_component>();

		++id;
	}

	return true;
}

bool projector_network_server::createSettingsMessage(send_buffer& buffer)
{
	buffer.header.type = message_server_solver_settings;
	buffer.header.messageID = runningMessageID++;
	return buffer.pushValue(manager->solver.settings);
}

bool projector_network_server::createObjectUpdateMessage(send_buffer& buffer)
{
	auto objectGroup = getObjectGroup(scene);
	uint32 numObjectsInScene = (uint32)objectGroup.size();

	buffer.header.type = message_server_object_update;
	buffer.header.messageID = runningMessageID++;

	server_object_update_message* messages = buffer.push<server_object_update_message>(numObjectsInScene);
	if (!messages)
	{
		LOG_ERROR("Not enough space in message buffer to fit %u object updates", numObjectsInScene);
		return false;
	}

	uint32 id = 0;
	for (auto [entityHandle, raster, transform] : objectGroup.each())
	{
		server_object_update_message& msg = messages[id];

		memcpy(msg.rotation, transform.rotation.v4.data, sizeof(quat));
		memcpy(msg.position, transform.position.data, sizeof(vec3));
		msg.id = (uint32)entityHandle;

		++id;
	}

	return true;
}

bool projector_network_server::createProjectorInstantiationMessage(struct send_buffer& buffer, const std::vector<projector_instantiation>& instantiations)
{
	buffer.header.type = message_server_projector_instantiation;
	buffer.header.messageID = runningMessageID++;

	uint32 count = (uint32)instantiations.size();
	projector_instantiation* messages = buffer.push<projector_instantiation>(count);
	if (!messages)
	{
		LOG_ERROR("Not enough space in message buffer to fit %u projector instantiations", count);
		return false;
	}

	memcpy(messages, instantiations.data(), count * sizeof(projector_instantiation));

	return true;
}

bool projector_network_server::sendToAllClients(send_buffer& buffer)
{
	bool result = true;

	buffer.header.messageID = runningMessageID++;

	for (const client_connection& connection : clientConnections)
	{
		buffer.header.clientID = connection.clientID;
		result &= serverSocket.send(connection.address, buffer.buffer, buffer.size);
	}

	return result;
}

bool projector_network_server::sendToClient(send_buffer& buffer, const client_connection& connection)
{
	buffer.header.messageID = runningMessageID++;
	buffer.header.clientID = connection.clientID;

	return serverSocket.send(connection.address, buffer.buffer, buffer.size);
}

bool projector_network_client::update()
{
	if (!connected)
	{
		return false;
	}


	if (memcmp(&manager->solver.settings, &oldSolverSettings, sizeof(oldSolverSettings)) != 0)
	{
		oldSolverSettings = manager->solver.settings;

		send_buffer messageBuffer;
		messageBuffer.header.type = message_client_request_calibration_mode;
		messageBuffer.pushValue(manager->solver.settings.mode);

		sendToServer(messageBuffer);
	}





	receive_buffer messageBuffer;

	while (true)
	{
		network_address senderAddress;
		receive_result result = clientSocket.receive(senderAddress, messageBuffer.buffer, sizeof(messageBuffer.buffer), messageBuffer.actualSize);

		if (result == receive_result_nothing_received)
		{
			break;
		}

		if (senderAddress != serverAddress)
		{
			LOG_MESSAGE("Received message from sender other than server. Ignoring message");
			continue;
		}

		if (result == receive_result_connection_closed)
		{
			LOG_MESSAGE("Connection closed");
			connected = false;
			return false;
		}

		messageBuffer.reset();


		const message_header* header = messageBuffer.get<message_header>(1);
		if (!header)
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), messageBuffer.sizeRemaining);
			continue;
		}

		if (clientID == -1)
		{
			LOG_MESSAGE("Received first message from server. Assigning client ID %u", header->clientID);
			clientID = header->clientID;
		}

		if (clientID != header->clientID)
		{
			LOG_ERROR("Received message with non-matching client ID. Expected %u, got %u. Ignoring message", clientID, (uint32)header->clientID);
			continue;
		}

		switch (header->type)
		{
			case message_server_solver_settings:
			{
				if (messageBuffer.sizeRemaining != sizeof(projector_solver_settings))
				{
					LOG_ERROR("Message size does not equal sizeof(projector_solver_settings). Expected %u, got %u", (uint32)sizeof(projector_solver_settings), messageBuffer.sizeRemaining);
					break;
				}

				if (header->messageID < latestSettingsMessageID)
				{
					// Ignore out-of-order tracking messages.
					break;
				}

				latestSettingsMessageID = header->messageID;

				LOG_MESSAGE("Updating solver settings");

				manager->solver.settings = *messageBuffer.get<projector_solver_settings>(1);
				oldSolverSettings = manager->solver.settings;
			} break;

			case message_server_object_info:
			{
				if (messageBuffer.sizeRemaining % sizeof(server_object_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(server_object_message). Expected multiple of %u, got %u", (uint32)sizeof(server_object_message), messageBuffer.sizeRemaining);
					break;
				}

				if (header->messageID < latestObjectMessageID)
				{
					break;
				}

				latestObjectMessageID = header->messageID;

				// Delete objects in scene.
				auto objectGroup = getObjectGroup(scene);
				scene->registry.destroy(objectGroup.begin(), objectGroup.end());
				objectIDToEntity.clear();

				// Populate with received objects.
				uint32 numObjects = messageBuffer.sizeRemaining / (uint32)sizeof(server_object_message);
				const server_object_message* objects = messageBuffer.get<server_object_message>(numObjects);

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

						if (objects[i].tracked)
						{
							targetObject.addComponent<tracking_component>();
						}

						objectIDToEntity[objects[i].id] = targetObject;
					}
				}

			} break;

			case message_server_object_update:
			{
				if (messageBuffer.sizeRemaining % sizeof(server_object_update_message) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(server_object_update_message). Expected multiple of %u, got %u", (uint32)sizeof(server_object_update_message), messageBuffer.sizeRemaining);
					break;
				}

				if (header->messageID < latestObjectUpdateMessageID)
				{
					break;
				}

				latestObjectUpdateMessageID = header->messageID;

				uint32 numObjects = messageBuffer.sizeRemaining / (uint32)sizeof(server_object_update_message);
				const server_object_update_message* objects = messageBuffer.get<server_object_update_message>(numObjects);

				//LOG_MESSAGE("Received message containing %u objects", numObjects);

				for (uint32 i = 0; i < numObjects; ++i)
				{
					auto it = objectIDToEntity.find(objects[i].id);
					if (it != objectIDToEntity.end())
					{
						scene_entity e = it->second;

						transform_component& transform = e.getComponent<transform_component>();
						memcpy(transform.rotation.v4.data, objects[i].rotation, sizeof(quat));
						memcpy(transform.position.data, objects[i].position, sizeof(vec3));
					}
				}

			} break;

			case message_server_projector_instantiation:
			{
				if (messageBuffer.sizeRemaining % sizeof(projector_instantiation) != 0)
				{
					LOG_ERROR("Message size is not evenly divisible by sizeof(projector_instantiation). Expected multiple of %u, got %u", (uint32)sizeof(projector_instantiation), messageBuffer.sizeRemaining);
					break;
				}

				if (header->messageID < latestProjectorInstantiationMessageID)
				{
					break;
				}

				latestProjectorInstantiationMessageID = header->messageID;

				uint32 numProjectors = messageBuffer.sizeRemaining / (uint32)sizeof(projector_instantiation);
				const projector_instantiation* projectors = messageBuffer.get<projector_instantiation>(numProjectors);

				assert(messageBuffer.sizeRemaining == 0);

				std::vector<projector_instantiation> instantiations(numProjectors);
				memcpy(instantiations.data(), projectors, numProjectors * sizeof(projector_instantiation));

				manager->network_projectorInstantiations(instantiations);

			} break;

		};
	}

	return true;
}

bool projector_network_client::sendHello()
{
	send_buffer messageBuffer;
	messageBuffer.header.type = message_client_hello;
	messageBuffer.header.clientID = -1;
	messageBuffer.header.messageID = 0;

	std::vector<monitor_info>& monitors = win32_window::allConnectedMonitors;

	client_hello_message hello;
	gethostname(hello.hostname, sizeof(hello.hostname));

	uint32 numMonitors = (uint32)monitors.size();

	messageBuffer.pushValue(hello);

	client_monitor_info* messages = messageBuffer.push<client_monitor_info>(numMonitors);

	for (uint32 i = 0; i < numMonitors; ++i)
	{
		client_monitor_info& msg = messages[i];
		monitor_info& mon = monitors[i];

		strncpy_s(msg.description, mon.description.c_str(), sizeof(msg.description));
		strncpy_s(msg.uniqueID, mon.uniqueID.c_str(), sizeof(msg.uniqueID));
	}

	return sendToServer(messageBuffer);
}

bool projector_network_client::reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calibs)
{
	return false;
}

bool projector_network_client::sendToServer(send_buffer& buffer)
{
	return clientSocket.send(serverAddress, buffer.buffer, buffer.size);
}
