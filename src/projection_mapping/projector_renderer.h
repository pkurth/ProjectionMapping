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

	static void beginFrameCommon();
	void beginFrame(uint32 windowWidth, uint32 windowHeight);
	void endFrame();
	void finalizeImage(dx_command_list* cl);


	// Set these with your application.
	void setProjectorCamera(const render_camera& camera);


	static void setViewerCamera(const render_camera& camera);
	static void setEnvironment(const ref<pbr_environment>& environment);
	static void setSun(const directional_light& light);
	static void setPointLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer);
	static void setSpotLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer);


	void submitRenderPass(const opaque_render_pass* renderPass) { assert(!opaqueRenderPass); opaqueRenderPass = renderPass; }

	uint32 renderWidth;
	uint32 renderHeight;
	ref<dx_texture> frameResult;

	static inline tonemap_settings tonemapSettings;


	static inline bool applySolverIntensity = false;

private:

	bool active = true;

	static void present(dx_command_list* cl,
		ref<dx_texture> ldrInput,
		ref<dx_texture> solverIntensity,
		ref<dx_texture> output,
		sharpen_settings sharpenSettings);

	const opaque_render_pass* opaqueRenderPass = 0;


	ref<dx_texture> hdrColorTexture;
	ref<dx_texture> worldNormalsTexture;
	ref<dx_texture> reflectanceTexture;
	ref<dx_texture> depthStencilBuffer;

	ref<dx_texture> solverIntensityTexture;
	ref<dx_texture> solverIntensityTempTexture;

	ref<dx_texture> attenuationTexture;
	ref<dx_texture> maskTexture;

	ref<dx_texture> halfResolutionDepthBuffer;
	ref<dx_texture> halfResolutionColorTexture;

	ref<dx_texture> bestMaskTexture;
	ref<dx_texture> depthDiscontinuitiesTexture;
	ref<dx_texture> colorDiscontinuitiesTexture;

	ref<dx_texture> dilateTempTexture;
	
	ref<dx_texture> hdrPostProcessingTexture;
	ref<dx_texture> ldrPostProcessingTexture;


	light_culling culling;

	camera_cb projectorCamera;





	static inline ref<pbr_environment> environment;
	static inline ref<dx_buffer> pointLights;
	static inline ref<dx_buffer> spotLights;
	static inline ref<dx_buffer> pointLightShadowInfoBuffer;
	static inline ref<dx_buffer> spotLightShadowInfoBuffer;
	static inline uint32 numPointLights = 0;
	static inline uint32 numSpotLights = 0;

	static inline camera_cb viewerCamera;
	static inline directional_light_cb sun;

	friend struct projector_manager;
	friend struct projector_solver;
};
