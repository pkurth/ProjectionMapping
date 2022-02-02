#pragma once

#include "scene/scene.h"
#include "core/camera_controller.h"
#include "core/system.h"
#include "undo_stack.h"
#include "transformation_gizmo.h"
#include "rendering/main_renderer.h"
#include "tracking/tracking.h"
#include "projection_mapping/projector_manager.h"
#include "calibration/calibration.h"

struct scene_editor
{
	void initialize(game_scene* scene, main_renderer* renderer, depth_tracker* tracker, projector_manager* projectorManager, projector_system_calibration* projectorCalibration);

	bool update(const user_input& input, ldr_render_pass* ldrRenderPass, float dt);

	scene_entity selectedEntity;

	void setSelectedEntity(scene_entity entity);
	void setEnvironment(const fs::path& filename);

private:
	void drawSettings(float dt);
	void drawMainMenuBar();
	bool drawSceneHierarchy();
	void drawHardwareWindow();
	bool handleUserInput(const user_input& input, ldr_render_pass* ldrRenderPass, float dt);
	void drawEntityCreationPopup();

	void updateSelectedEntityUIRotation();

	void setSelectedEntityNoUndo(scene_entity entity);

	void serializeToFile();
	bool deserializeFromFile();

	game_scene* scene;
	main_renderer* renderer;
	depth_tracker* tracker;
	projector_manager* projectorManager;
	projector_system_calibration* projectorCalibration;

	undo_stack undoStack;
	transformation_gizmo gizmo;

	camera_controller cameraController;

	vec3 selectedEntityEulerRotation;

	system_info systemInfo;

	friend struct selection_undo;
};
