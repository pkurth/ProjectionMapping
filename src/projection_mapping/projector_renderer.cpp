#include "pch.h"
#include "projector_renderer.h"
#include "rendering/render_resources.h"
#include "dx/dx_profiling.h"
#include "dx/dx_barrier_batcher.h"

#include "projector_rs.hlsli"


static dx_pipeline projectorPresentPipeline;

void projector_renderer::initializeCommon()
{
	projectorPresentPipeline = createReloadablePipeline("projector_present_cs");
}

void projector_renderer::initialize(color_depth colorDepth, uint32 windowWidth, uint32 windowHeight)
{
	this->renderWidth = windowWidth;
	this->renderHeight = windowHeight;

	hdrColorTexture = createTexture(0, renderWidth, renderHeight, hdrFormat, false, true, true, D3D12_RESOURCE_STATE_RENDER_TARGET);
	SET_NAME(hdrColorTexture->resource, "HDR Color");

	worldNormalsTexture = createTexture(0, renderWidth, renderHeight, worldNormalsFormat, false, true, false, D3D12_RESOURCE_STATE_RENDER_TARGET);
	SET_NAME(worldNormalsTexture->resource, "World normals");

	reflectanceTexture = createTexture(0, renderWidth, renderHeight, reflectanceFormat, false, true, false, D3D12_RESOURCE_STATE_RENDER_TARGET);
	SET_NAME(reflectanceTexture->resource, "Reflectance");

	depthStencilBuffer = createDepthTexture(renderWidth, renderHeight, depthStencilFormat);
	SET_NAME(depthStencilBuffer->resource, "Depth buffer");

	hdrPostProcessingTexture = createTexture(0, renderWidth, renderHeight, hdrFormat, false, true, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(hdrPostProcessingTexture->resource, "HDR Post processing");

	ldrPostProcessingTexture = createTexture(0, renderWidth, renderHeight, ldrFormat, false, true, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(ldrPostProcessingTexture->resource, "LDR Post processing");

	frameResult = createTexture(0, renderWidth, renderHeight, colorDepthToFormat(colorDepth), false, true, true);
	SET_NAME(frameResult->resource, "Frame result");

	solverIntensityTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16_FLOAT, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(solverIntensityTexture->resource, "Solver intensity");

	solverIntensityTempTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16_UNORM, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(solverIntensityTempTexture->resource, "Solver intensity temp");

	attenuationTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16G16_FLOAT, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(attenuationTexture->resource, "Attenuation");

	maskTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16G16_FLOAT, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(maskTexture->resource, "Mask");

	halfResolutionDepthBuffer = createTexture(0, renderWidth / 2, renderHeight / 2, DXGI_FORMAT_R16_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(halfResolutionDepthBuffer->resource, "Half resolution depth buffer");

	halfResolutionColorTexture = createTexture(0, renderWidth / 2, renderHeight / 2, ldrFormat, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(halfResolutionColorTexture->resource, "Half resolution color texture");

	bestMaskTexture = createTexture(0, renderWidth / 2, renderHeight / 2, DXGI_FORMAT_R8_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(bestMaskTexture->resource, "Best mask");

	depthDiscontinuitiesTexture = createTexture(0, renderWidth / 2, renderHeight / 2, DXGI_FORMAT_R16_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(depthDiscontinuitiesTexture->resource, "Depth discontinuities");

	colorDiscontinuitiesTexture = createTexture(0, renderWidth / 2, renderHeight / 2, DXGI_FORMAT_R16_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(colorDiscontinuitiesTexture->resource, "Color discontinuities");

	dilateTempTexture = createTexture(0, renderWidth / 2, renderHeight / 2, DXGI_FORMAT_R16_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(dilateTempTexture->resource, "Depth dilate temp");
}

void projector_renderer::shutdown()
{
	opaqueRenderPass = 0;
	
	hdrColorTexture = 0;
	worldNormalsTexture = 0;
	reflectanceTexture = 0;
	depthStencilBuffer = 0;

	hdrPostProcessingTexture = 0;
	ldrPostProcessingTexture = 0;

	solverIntensityTexture = 0;
	solverIntensityTempTexture = 0;

	attenuationTexture = 0;
	maskTexture = 0;

	halfResolutionDepthBuffer = 0;
	halfResolutionColorTexture = 0;

	bestMaskTexture = 0;
	depthDiscontinuitiesTexture = 0;
	colorDiscontinuitiesTexture = 0;
	dilateTempTexture = 0;
}

void projector_renderer::beginFrameCommon()
{
	environment = 0;

	pointLights = 0;
	spotLights = 0;
	numPointLights = 0;
	numSpotLights = 0;
}

void projector_renderer::beginFrame(uint32 windowWidth, uint32 windowHeight)
{
	opaqueRenderPass = 0;

	active = windowWidth > 0 && windowHeight > 0;
	if (!active)
	{
		return;
	}

	if (this->renderWidth != windowWidth || this->renderHeight != windowHeight)
	{
		this->renderWidth = windowWidth;
		this->renderHeight = windowHeight;

		resizeTexture(hdrColorTexture, renderWidth, renderHeight);
		resizeTexture(worldNormalsTexture, renderWidth, renderHeight);
		resizeTexture(reflectanceTexture, renderWidth, renderHeight);
		resizeTexture(depthStencilBuffer, renderWidth, renderHeight);

		resizeTexture(hdrPostProcessingTexture, renderWidth, renderHeight);
		resizeTexture(ldrPostProcessingTexture, renderWidth, renderHeight);

		resizeTexture(frameResult, renderWidth, renderHeight);

		resizeTexture(solverIntensityTexture, renderWidth, renderHeight);
		resizeTexture(solverIntensityTempTexture, renderWidth, renderHeight);

		resizeTexture(attenuationTexture, renderWidth, renderHeight);
		resizeTexture(maskTexture, renderWidth, renderHeight);

		resizeTexture(halfResolutionDepthBuffer, renderWidth / 2, renderHeight / 2);
		resizeTexture(halfResolutionColorTexture, renderWidth / 2, renderHeight / 2);

		resizeTexture(bestMaskTexture, renderWidth / 2, renderHeight / 2);
		resizeTexture(depthDiscontinuitiesTexture, renderWidth / 2, renderHeight / 2);
		resizeTexture(colorDiscontinuitiesTexture, renderWidth / 2, renderHeight / 2);
		resizeTexture(dilateTempTexture, renderWidth / 2, renderHeight / 2);
	}

	culling.allocateIfNecessary(renderWidth, renderHeight);
}

void projector_renderer::setProjectorCamera(const render_camera& camera)
{
	buildCameraConstantBuffer(camera, 0.f, this->projectorCamera);
}

void projector_renderer::setViewerCamera(const render_camera& camera)
{
	buildCameraConstantBuffer(camera, 0.f, viewerCamera);
}

void projector_renderer::setEnvironment(const ref<pbr_environment>& env)
{
	environment = env;
}

void projector_renderer::setSun(const directional_light& light)
{
	sun.direction = light.direction;
	sun.radiance = light.color * light.intensity;
	sun.numShadowCascades = 0;
}

void projector_renderer::setPointLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer)
{
	pointLights = lights;
	numPointLights = numLights;
	pointLightShadowInfoBuffer = shadowInfoBuffer;
}

void projector_renderer::setSpotLights(const ref<dx_buffer>& lights, uint32 numLights, const ref<dx_buffer>& shadowInfoBuffer)
{
	spotLights = lights;
	numSpotLights = numLights;
	spotLightShadowInfoBuffer = shadowInfoBuffer;
}

void projector_renderer::endFrame()
{
	if (!active)
	{
		return;
	}

	auto projectorCameraCBV = dxContext.uploadDynamicConstantBuffer(projectorCamera);
	auto viewerCameraCBV = dxContext.uploadDynamicConstantBuffer(viewerCamera);
	auto sunCBV = dxContext.uploadDynamicConstantBuffer(sun);

	common_material_info materialInfo;
	if (environment)
	{
		materialInfo.sky = environment->sky;
		materialInfo.environment = environment->environment;
		materialInfo.irradiance = environment->irradiance;
	}
	else
	{
		materialInfo.sky = render_resources::blackCubeTexture;
		materialInfo.environment = render_resources::blackCubeTexture;
		materialInfo.irradiance = render_resources::blackCubeTexture;
	}
	materialInfo.environmentIntensity = 1.f;
	materialInfo.skyIntensity = 1.f;
	materialInfo.aoTexture = render_resources::whiteTexture;
	materialInfo.sssTexture = render_resources::whiteTexture;
	materialInfo.tiledCullingGrid = culling.tiledCullingGrid;
	materialInfo.tiledObjectsIndexList = culling.tiledObjectsIndexList;
	materialInfo.pointLightBuffer = pointLights;
	materialInfo.spotLightBuffer = spotLights;
	materialInfo.decalBuffer = 0;
	materialInfo.shadowMap = render_resources::shadowMap;
	materialInfo.decalTextureAtlas = 0;
	materialInfo.pointLightShadowInfoBuffer = pointLightShadowInfoBuffer;
	materialInfo.spotLightShadowInfoBuffer = spotLightShadowInfoBuffer;
	materialInfo.volumetricsTexture = 0;
	materialInfo.cameraCBV = viewerCameraCBV;
	materialInfo.sunCBV = sunCBV;


	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	{
		PROFILE_ALL(cl, "Projector");

		cl->clearDepthAndStencil(depthStencilBuffer);
		cl->clearRTV(hdrColorTexture, 0.f, 0.f, 0.f, 0.f); // This replaces the sky, which is not rendered for projectors. Clear alpha to zero too, to indicate background.
		cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


		auto depthOnlyRenderTarget = dx_render_target(renderWidth, renderHeight)
			.depthAttachment(depthStencilBuffer);

		depthPrePass(cl, depthOnlyRenderTarget, opaqueRenderPass,
			projectorCamera.viewProj, projectorCamera.prevFrameViewProj, projectorCamera.jitter, projectorCamera.prevFrameJitter);


		cl->transitionBarrier(depthStencilBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		lightAndDecalCulling(cl, depthStencilBuffer, pointLights, spotLights, 0, culling, numPointLights, numSpotLights, 0, projectorCameraCBV);
		cl->transitionBarrier(depthStencilBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);




		auto hdrOpaqueRenderTarget = dx_render_target(renderWidth, renderHeight)
			.colorAttachment(hdrColorTexture)
			.colorAttachment(worldNormalsTexture)
			.colorAttachment(reflectanceTexture)
			.depthAttachment(depthStencilBuffer);

		opaqueLightPass(cl, hdrOpaqueRenderTarget, opaqueRenderPass, materialInfo, projectorCamera.viewProj);


		barrier_batcher(cl)
			.transition(hdrColorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			.transition(worldNormalsTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			.transition(reflectanceTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);


		specularAmbient(cl, hdrColorTexture, 0, worldNormalsTexture, reflectanceTexture,
			environment ? environment->environment : 0, render_resources::whiteTexture, hdrPostProcessingTexture, materialInfo.cameraCBV);

		barrier_batcher(cl)
			//.uav(hdrPostProcessingTexture)
			.transition(hdrPostProcessingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); // Will be read by rest of post processing stack. 


		ref<dx_texture> hdrResult = hdrPostProcessingTexture; // Specular highlights have been rendered to this texture. It's in read state.

		tonemap(cl, hdrResult, ldrPostProcessingTexture, tonemapSettings);

		barrier_batcher(cl)
			.transition(hdrColorTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			.transition(hdrPostProcessingTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			.transition(worldNormalsTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			.transition(reflectanceTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			.transition(ldrPostProcessingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	dxContext.executeCommandList(cl);
}

void projector_renderer::present(dx_command_list* cl,
	ref<dx_texture> ldrInput,
	ref<dx_texture> solverIntensity,
	ref<dx_texture> output,
	sharpen_settings sharpenSettings)
{
	DX_PROFILE_BLOCK(cl, "Present");

	cl->setPipelineState(*projectorPresentPipeline.pipeline);
	cl->setComputeRootSignature(*projectorPresentPipeline.rootSignature);

	cl->setDescriptorHeapUAV(PROJECTOR_PRESENT_RS_TEXTURES, 0, output);
	cl->setDescriptorHeapSRV(PROJECTOR_PRESENT_RS_TEXTURES, 1, ldrInput);
	cl->setDescriptorHeapSRV(PROJECTOR_PRESENT_RS_TEXTURES, 2, solverIntensity);
	cl->setCompute32BitConstants(PROJECTOR_PRESENT_RS_CB, present_cb{ present_sdr, 0.f, sharpenSettings.strength, 0 });

	cl->dispatch(bucketize(output->width, PROJECTOR_BLOCK_SIZE), bucketize(output->height, PROJECTOR_BLOCK_SIZE));
}

void projector_renderer::finalizeImage(dx_command_list* cl)
{
	if (!active)
	{
		return;
	}

	barrier_batcher(cl)
		.transition(frameResult, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	if (applySolverIntensity)
	{
		present(cl, ldrPostProcessingTexture, solverIntensityTexture, frameResult, sharpen_settings{ 0.f });
	}
	else
	{
		::present(cl, ldrPostProcessingTexture, frameResult, sharpen_settings{ 0.f });
	}

	barrier_batcher(cl)
		.transition(ldrPostProcessingTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		.transition(frameResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
}

