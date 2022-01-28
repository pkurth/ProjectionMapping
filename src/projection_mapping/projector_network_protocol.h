#pragma once

#include "scene/scene.h"
#include "projector_manager.h"

extern char SERVER_IP[128];
extern uint32 SERVER_PORT;

extern bool projectorNetworkInitialized;

bool startProjectorNetworkProtocol(game_scene& scene, projector_manager* manager, bool isServer);
bool updateProjectorNetworkProtocol(float dt);

bool notifyProjectorNetworkOnSceneLoad(const projector_context& context, const std::vector<std::string>& projectors);
