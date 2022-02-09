#include "pch.h"
#include "projector_network_protocol.h"
#include "projector_manager.h"

#include "network/socket.h"

#include "core/log.h"
#include "window/window.h"
#include "core/file_registry.h"


enum message_type : uint16
{
	message_hello_from_client,
	message_solver_settings
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
	T& pushValue(const T& t)
	{
		T* result = push<T>(1);
		*result = t;
		return *result;
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
	T* get(uint32 count)
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
	connected = true;

	char clientAddress[128];
	getLocalIPAddress(clientAddress);
	LOG_MESSAGE("Client created, IP: %s", clientAddress);

	sendHello();

	return true;
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
			messageBuffer.header.type = message_solver_settings;

			messageBuffer.pushValue(manager->solver.settings);

			sendToAllClients(messageBuffer);
		}

		timeSinceLastUpdate -= updateTime;
	}




	receive_buffer messageBuffer;
	network_address clientAddress;

	while (serverSocket.receive(clientAddress, messageBuffer.buffer, sizeof(messageBuffer.buffer), messageBuffer.actualSize) == receive_result_success)
	{
		messageBuffer.reset();

		const message_header* header = messageBuffer.get<message_header>(1);
		if (!header)
		{
			LOG_ERROR("Message is not even large enough for the header. Expected at least %u, got %u", (uint32)sizeof(message_header), messageBuffer.sizeRemaining);
			continue;
		}

		switch (header->type)
		{
			case message_hello_from_client:
			{
				uint16 clientID = runningClientID++;
				LOG_MESSAGE("New client connected. Assigning new client ID %u", clientID);

				client_connection connection = { clientID, clientAddress };
				clientConnections.push_back(connection);
			}
		}
	}





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

bool projector_network_client::update()
{
	if (!connected)
	{
		return false;
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
			case message_solver_settings:
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
			} break;



		};
	}

	return true;
}

void projector_network_client::sendHello()
{
	send_buffer messageBuffer;
	messageBuffer.header.type = message_hello_from_client;
	messageBuffer.header.clientID = -1;
	messageBuffer.header.messageID = 0;

	sendToServer(messageBuffer);
}

bool projector_network_client::sendToServer(send_buffer& buffer)
{
	return clientSocket.send(serverAddress, buffer.buffer, buffer.size);
}
