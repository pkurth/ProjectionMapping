#pragma once

#include "scene.h"
#include "core/camera.h"
#include "rendering/main_renderer.h"
#include "tracking/tracking.h"
#include "projection_mapping/projector.h"

void serializeSceneToDisk(game_scene& scene, const renderer_settings& rendererSettings, depth_tracker* tracker, projector_context* projectorContext);
bool deserializeSceneFromDisk(game_scene& scene, renderer_settings& rendererSettings, std::string& environmentName, depth_tracker* tracker, projector_context* projectorContext);

bool deserializeProjectorCalibrationOnly(projector_context* projectorContext, vec3 globalCameraPosition, quat globalCameraRotation);
