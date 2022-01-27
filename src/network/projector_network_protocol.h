#pragma once

#include "scene/scene.h"

extern bool projectorNetworkInitialized;

bool startProjectorNetworkProtocol(game_scene& scene, bool isServer);
bool updateProjectorNetworkProtocol(float dt);

