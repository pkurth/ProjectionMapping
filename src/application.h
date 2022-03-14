#pragma once

#include "core/input.h"
#include "core/camera.h"
#include "core/camera_controller.h"
#include "geometry/mesh.h"
#include "core/math.h"
#include "scene/scene.h"
#include "rendering/main_renderer.h"
#include "rendering/raytracing.h"
#include "editor/editor.h"
#include "projection_mapping/projector_manager.h"
#include "calibration/calibration.h"
#include "tracking/tracking.h"

struct application
{
	void loadCustomShaders();
	void initialize(main_renderer* renderer, projector_manager* projectorManager, projector_system_calibration* projectorCalibration, depth_tracker* tracker);
	void update(const user_input& input, float dt);

	void handleFileDrop(const fs::path& filename);

	game_scene& getScene() { return scene; }
	scene_editor& getEditor() { return editor; }

private:

	void resetRenderPasses();
	void submitRenderPasses(uint32 numSpotLightShadowPasses, uint32 numPointLightShadowPasses);


	raytracing_tlas raytracingTLAS;



	ref<dx_buffer> pointLightBuffer[NUM_BUFFERED_FRAMES];
	ref<dx_buffer> spotLightBuffer[NUM_BUFFERED_FRAMES];
	ref<dx_buffer> decalBuffer[NUM_BUFFERED_FRAMES];

	ref<dx_buffer> spotLightShadowInfoBuffer[NUM_BUFFERED_FRAMES];
	ref<dx_buffer> pointLightShadowInfoBuffer[NUM_BUFFERED_FRAMES];

	main_renderer* renderer;
	projector_manager* projectorManager;
	depth_tracker* tracker;
	projector_system_calibration* projectorCalibration;

	game_scene scene;
	scene_editor editor;

	memory_arena stackArena;


	uint32 numSpotShadowRenderPasses;
	uint32 numPointShadowRenderPasses;

	opaque_render_pass opaqueRenderPass;
	opaque_render_pass projectorOpaqueRenderPass;
	transparent_render_pass transparentRenderPass;
	sun_shadow_render_pass sunShadowRenderPass;
	spot_shadow_render_pass spotShadowRenderPasses[16];
	point_shadow_render_pass pointShadowRenderPasses[16];
	ldr_render_pass ldrRenderPass;

	bool visualizeProjIntensities = false;

};
