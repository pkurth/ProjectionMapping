#pragma once

#include "core/math.h"
#include "core/camera.h"
#include "render_pass.h"
#include "light_source.h"
#include "pbr.h"
#include "core/input.h"
#include "raytracer.h"
#include "render_algorithms.h"
#include "render_resources.h"
#include "render_utils.h"
#include "path_tracing.h"

#include "light_source.hlsli"
#include "camera.hlsli"
#include "path_tracing.h"




struct renderer_settings
{
	tonemap_settings tonemapSettings;
	float environmentIntensity = 1.f;
	float skyIntensity = 1.f;

	bool enableAO = true;
	hbao_settings aoSettings;

	bool enableSSS = true;
	sss_settings sssSettings;

	bool enableSSR = true;
	ssr_settings ssrSettings;

	bool enableTAA = true;
	taa_settings taaSettings;

	bool enableBloom = true;
	bloom_settings bloomSettings;

	bool enableSharpen = true;
	sharpen_settings sharpenSettings;
};


enum aspect_ratio_mode
{
	aspect_ratio_free,
	aspect_ratio_fix_16_9,
	aspect_ratio_fix_16_10,

	aspect_ratio_mode_count,
};

static const char* aspectRatioNames[] =
{
	"Free",
	"16:9",
	"16:10",
};

enum renderer_mode
{
	renderer_mode_default,
	renderer_mode_visualize_sun_shadow_cascades,

	renderer_mode_count,
};

static const char* rendererModeNames[] =
{
	"Default",
	"Visualize sun shadow cascades",
};


struct renderer_spec
{
	bool allowObjectPicking = true;
	bool allowAO = true;
	bool allowSSS = true;
	bool allowSSR = true;
	bool allowTAA = true;
	bool allowBloom = true;
};

struct main_renderer
{
	main_renderer() {}
	void initialize(color_depth colorDepth, uint32 windowWidth, uint32 windowHeight, renderer_spec spec);

	void beginFrame(uint32 windowWidth, uint32 windowHeight);
	void endFrame(const user_input& input);

	void takeScreenShot(const fs::path& path);

	// Set these with your application.
	void setCamera(const render_camera& camera);
	void setEnvironment(const ref<pbr_environment>& environment);
	void setSun(const directional_light& light);

	void setPointLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer);
	void setSpotLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer);
	void setDecals(const ref<dx_buffer>& decals, uint32 numDecals, const ref<dx_texture>& textureAtlas);

	void submitRenderPass(opaque_render_pass* renderPass) {	assert(!opaqueRenderPass); opaqueRenderPass = renderPass; }
	void submitRenderPass(transparent_render_pass* renderPass) { assert(!transparentRenderPass); transparentRenderPass = renderPass; }
	void submitRenderPass(ldr_render_pass* renderPass) { assert(!ldrRenderPass); ldrRenderPass = renderPass; }
	void setRaytracingScene(raytracing_tlas* tlas) { this->tlas = tlas;	}



	// Settings.
	renderer_mode mode = renderer_mode_default;
	aspect_ratio_mode aspectRatioMode = aspect_ratio_free;


	renderer_settings settings;
	bool disableAllPostProcessing = false;



	uint32 renderWidth;
	uint32 renderHeight;
	ref<dx_texture> frameResult;

	uint32 hoveredObjectID = -1;

	const renderer_spec spec;


	const ref<dx_texture>& getAOResult() const { return aoTextures[aoHistoryIndex]; }
	const ref<dx_texture>& getSSSResult() const { return sssTextures[sssHistoryIndex]; }
	const ref<dx_texture>& getSSRResult() const { return ssrResolveTexture; }
	const ref<dx_texture>& getBloomResult() const { return bloomTexture; }

private:

	raytracing_tlas* tlas;

	const opaque_render_pass* opaqueRenderPass;
	const transparent_render_pass* transparentRenderPass;
	const ldr_render_pass* ldrRenderPass;


	uint32 windowWidth;
	uint32 windowHeight;
	int32 windowXOffset = 0;
	int32 windowYOffset = 0;

	bool windowHovered = false;

	ref<dx_texture> hdrColorTexture;
	ref<dx_texture> worldNormalsTexture;
	ref<dx_texture> screenVelocitiesTexture;
	ref<dx_texture> objectIDsTexture;
	ref<dx_texture> reflectanceTexture;
	ref<dx_texture> depthStencilBuffer;
	ref<dx_texture> linearDepthBuffer;
	ref<dx_texture> opaqueDepthBuffer; // The depth-stencil buffer gets copied to this texture after the opaque pass.

	ref<dx_texture> aoCalculationTexture;
	ref<dx_texture> aoBlurTempTexture;
	ref<dx_texture> aoTextures[2]; // These get flip-flopped from frame to frame.
	uint32 aoHistoryIndex = 0;
	bool aoWasOnLastFrame = false;

	ref<dx_texture> sssCalculationTexture;
	ref<dx_texture> sssBlurTempTexture;
	ref<dx_texture> sssTextures[2]; // These get flip-flopped from frame to frame.
	uint32 sssHistoryIndex = 0;
	bool sssWasOnLastFrame = false;

	ref<dx_texture> ssrRaycastTexture;
	ref<dx_texture> ssrResolveTexture;
	ref<dx_texture> ssrTemporalTextures[2]; // These get flip-flopped from frame to frame.
	uint32 ssrHistoryIndex = 0;

	ref<dx_texture> prevFrameHDRColorTexture; // This is downsampled by a factor of 2 and contains up to 8 mip levels.
	ref<dx_texture> prevFrameHDRColorTempTexture;

	ref<dx_texture> hdrPostProcessingTexture;
	ref<dx_texture> ldrPostProcessingTexture;
	ref<dx_texture> taaTextures[2]; // These get flip-flopped from frame to frame.
	uint32 taaHistoryIndex = 0;

	ref<dx_texture> bloomTexture;
	ref<dx_texture> bloomTempTexture;

	ref<dx_buffer> hoveredObjectIDReadbackBuffer;

	ref<pbr_environment> environment;
	ref<dx_buffer> pointLights;
	ref<dx_buffer> spotLights;
	ref<dx_buffer> pointLightShadowInfoBuffer;
	ref<dx_buffer> spotLightShadowInfoBuffer;
	ref<dx_buffer> decals;
	uint32 numPointLights = 0;
	uint32 numSpotLights = 0;
	uint32 numDecals = 0;

	ref<dx_texture> decalTextureAtlas;

	camera_cb jitteredCamera;
	camera_cb unjitteredCamera;
	directional_light_cb sun;


	light_culling culling;


	aspect_ratio_mode oldAspectRatioMode = aspect_ratio_free;
	renderer_mode oldMode = renderer_mode_default;

	void recalculateViewport(bool resizeTextures);
};

