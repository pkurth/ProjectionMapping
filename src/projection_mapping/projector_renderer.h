#pragma once

#include "rendering/pbr.h"
#include "rendering/render_utils.h"
#include "rendering/render_algorithms.h"

struct projector_renderer
{
	static void initializeCommon();

	projector_renderer() {}
	void initialize(color_depth colorDepth, uint32 windowWidth, uint32 windowHeight);
	void shutdown();

	void beginFrame(uint32 windowWidth, uint32 windowHeight);
	void endFrame();
	void finalizeImage(dx_command_list* cl, bool applySolverIntensity);


	// Set these with your application.
	void setProjectorCamera(const render_camera& camera);
	void setViewerCamera(const render_camera& camera);
	void setEnvironment(const ref<pbr_environment>& environment);
	void setSun(const directional_light& light);

	void submitRenderPass(const opaque_render_pass* renderPass) { assert(!opaqueRenderPass); opaqueRenderPass = renderPass; }

	uint32 renderWidth;
	uint32 renderHeight;
	ref<dx_texture> frameResult;

	static tonemap_settings tonemapSettings;


private:

	static void present(dx_command_list* cl,
		ref<dx_texture> ldrInput,
		ref<dx_texture> solverIntensity,
		ref<dx_texture> output,
		sharpen_settings sharpenSettings);

	const opaque_render_pass* opaqueRenderPass;


	ref<dx_texture> hdrColorTexture;
	ref<dx_texture> worldNormalsTexture;
	ref<dx_texture> reflectanceTexture;
	ref<dx_texture> depthStencilBuffer;
	ref<dx_texture> solverIntensity;
	
	ref<dx_texture> hdrPostProcessingTexture;
	ref<dx_texture> ldrPostProcessingTexture;

	ref<pbr_environment> environment;

	camera_cb projectorCamera;
	camera_cb viewerCamera;
	directional_light_cb sun;

	friend struct projector_base;
	friend struct projector_manager;
};
