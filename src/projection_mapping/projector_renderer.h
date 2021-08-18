#pragma once

#include "rendering/pbr.h"
#include "rendering/render_utils.h"
#include "rendering/render_algorithms.h"

struct projector_renderer
{
	projector_renderer() {}
	void initialize(color_depth colorDepth, uint32 windowWidth, uint32 windowHeight);
	void shutdown();

	void beginFrame(uint32 windowWidth, uint32 windowHeight);
	void endFrame();


	// Set these with your application.
	void setProjectorCamera(const render_camera& camera);
	void setViewerCamera(const render_camera& camera);
	void setEnvironment(const ref<pbr_environment>& environment);
	void setSun(const directional_light& light);

	void submitRenderPass(opaque_render_pass* renderPass) { assert(!opaqueRenderPass); opaqueRenderPass = renderPass; }

	uint32 renderWidth;
	uint32 renderHeight;
	ref<dx_texture> frameResult;

	static tonemap_settings tonemapSettings;


private:

	const opaque_render_pass* opaqueRenderPass;


	ref<dx_texture> hdrColorTexture;
	ref<dx_texture> worldNormalsTexture;
	ref<dx_texture> reflectanceTexture;
	ref<dx_texture> depthStencilBuffer;
	
	ref<dx_texture> hdrPostProcessingTexture;
	ref<dx_texture> ldrPostProcessingTexture;

	ref<pbr_environment> environment;

	camera_cb projectorCamera;
	camera_cb viewerCamera;
	directional_light_cb sun;
};
