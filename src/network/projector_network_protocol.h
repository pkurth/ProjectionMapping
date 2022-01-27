#pragma once

#include "scene/scene.h"

extern char SERVER_IP[128];
extern uint32 SERVER_PORT;

extern bool projectorNetworkInitialized;

bool startProjectorNetworkProtocol(game_scene& scene, bool isServer);
bool updateProjectorNetworkProtocol(float dt);

