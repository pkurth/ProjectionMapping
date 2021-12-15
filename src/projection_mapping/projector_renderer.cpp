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

	realDepthStencilBuffer = createDepthTexture(renderWidth, renderHeight, depthStencilFormat);
	SET_NAME(realDepthStencilBuffer->resource, "Real depth buffer");

	hdrPostProcessingTexture = createTexture(0, renderWidth, renderHeight, hdrFormat, false, true, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(hdrPostProcessingTexture->resource, "HDR Post processing");

	ldrPostProcessingTexture = createTexture(0, renderWidth, renderHeight, ldrFormat, false, true, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(ldrPostProcessingTexture->resource, "LDR Post processing");

	frameResult = createTexture(0, renderWidth, renderHeight, colorDepthToFormat(colorDepth), false, true, true);
	SET_NAME(frameResult->resource, "Frame result");

	solverIntensityTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16_FLOAT, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(solverIntensityTexture->resource, "Solver intensity");

	solverIntensityTempTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R16_FLOAT, false, false, true, D3D12_RESOURCE_STATE_GENERIC_READ);
	SET_NAME(solverIntensityTempTexture->resource, "Solver intensity temp");

	depthDiscontinuitiesTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R8_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(depthDiscontinuitiesTexture->resource, "Depth discontinuities");

	depthDilateTempTexture = createTexture(0, renderWidth, renderHeight, DXGI_FORMAT_R8_UNORM, false, false, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	SET_NAME(depthDilateTempTexture->resource, "Depth dilate temp");
}

void projector_renderer::shutdown()
{
	opaqueRenderPass = 0;
	
	hdrColorTexture = 0;
	worldNormalsTexture = 0;
	reflectanceTexture = 0;
	depthStencilBuffer = 0;
	realDepthStencilBuffer = 0;

	hdrPostProcessingTexture = 0;
	ldrPostProcessingTexture = 0;

	solverIntensityTexture = 0;
	solverIntensityTempTexture = 0;

	depthDiscontinuitiesTexture = 0;
	depthDilateTempTexture = 0;

	environment = 0;
}

void projector_renderer::beginFrame(uint32 windowWidth, uint32 windowHeight)
{
	opaqueRenderPass = 0;
	environment = 0;

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
		resizeTexture(realDepthStencilBuffer, renderWidth, renderHeight);

		resizeTexture(hdrPostProcessingTexture, renderWidth, renderHeight);
		resizeTexture(ldrPostProcessingTexture, renderWidth, renderHeight);

		resizeTexture(frameResult, renderWidth, renderHeight);

		resizeTexture(solverIntensityTexture, renderWidth, renderHeight);
		resizeTexture(solverIntensityTempTexture, renderWidth, renderHeight);

		resizeTexture(depthDiscontinuitiesTexture, renderWidth, renderHeight);
		resizeTexture(depthDilateTempTexture, renderWidth, renderHeight);
	}
}

void projector_renderer::setProjectorCamera(const render_camera& camera)
{
	buildCameraConstantBuffer(camera, 0.f, this->projectorCamera);
}

void projector_renderer::setRealProjectorCamera(const render_camera& camera)
{
	buildCameraConstantBuffer(camera, 0.f, this->realProjectorCamera);
}

void projector_renderer::setViewerCamera(const render_camera& camera)
{
	buildCameraConstantBuffer(camera, 0.f, this->viewerCamera);
}

void projector_renderer::setEnvironment(const ref<pbr_environment>& environment)
{
	this->environment = environment;
}

void projector_renderer::setSun(const directional_light& light)
{
	sun.direction = light.direction;
	sun.radiance = light.color * light.intensity;
	sun.numShadowCascades = 0;
}

void projector_renderer::endFrame()
{
	if (!active)
	{
		return;
	}

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
	materialInfo.tiledCullingGrid = 0;
	materialInfo.tiledObjectsIndexList = 0;
	materialInfo.pointLightBuffer = 0;
	materialInfo.spotLightBuffer = 0;
	materialInfo.decalBuffer = 0;
	materialInfo.shadowMap = render_resources::shadowMap;
	materialInfo.decalTextureAtlas = 0;
	materialInfo.pointLightShadowInfoBuffer = 0;
	materialInfo.spotLightShadowInfoBuffer = 0;
	materialInfo.volumetricsTexture = 0;
	materialInfo.cameraCBV = viewerCameraCBV;
	materialInfo.sunCBV = sunCBV;


	dx_command_list* cl = dxContext.getFreeRenderCommandList();


	cl->clearDepthAndStencil(depthStencilBuffer);
	cl->clearDepthAndStencil(realDepthStencilBuffer);
	cl->clearRTV(hdrColorTexture, 0.f, 0.f, 0.f, 0.f); // This replaces the sky, which is not rendered for projectors. Clear alpha to zero too, to indicate background.
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	auto realDepthOnlyRenderTarget = dx_render_target(renderWidth, renderHeight)
		.depthAttachment(realDepthStencilBuffer);

	depthPrePass(cl, realDepthOnlyRenderTarget, opaqueRenderPass,
		realProjectorCamera.viewProj, realProjectorCamera.prevFrameViewProj, vec2(0.f, 0.f), vec2(0.f, 0.f));


	auto depthOnlyRenderTarget = dx_render_target(renderWidth, renderHeight)
		.depthAttachment(depthStencilBuffer);

	depthPrePass(cl, depthOnlyRenderTarget, opaqueRenderPass,
		projectorCamera.viewProj, projectorCamera.prevFrameViewProj, projectorCamera.jitter, projectorCamera.prevFrameJitter);


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
		.transition(hdrPostProcessingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) // Will be read by rest of post processing stack. 
		.transition(depthStencilBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);


	depthSobel(cl, depthStencilBuffer, depthDiscontinuitiesTexture, projectorCamera.projectionParams, depthDiscontinuityThreshold);

	barrier_batcher(cl)
		//.uav(depthDiscontinuitiesTexture)
		.transition(depthDiscontinuitiesTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	dilate(cl, depthDiscontinuitiesTexture, depthDilateTempTexture, depthDiscontinuityDilateRadius);
	if (blurDepthDiscontinuities)
	{
		gaussianBlur(cl, depthDiscontinuitiesTexture, depthDilateTempTexture, 0, 0, gaussian_blur_13x13, 4);
	}

	ref<dx_texture> hdrResult = hdrPostProcessingTexture; // Specular highlights have been rendered to this texture. It's in read state.

	tonemap(cl, hdrResult, ldrPostProcessingTexture, tonemapSettings);

	barrier_batcher(cl)
		.transition(depthStencilBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		.transition(depthDiscontinuitiesTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		.transition(hdrColorTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		.transition(hdrPostProcessingTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		.transition(worldNormalsTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		.transition(reflectanceTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);


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
		.transition(frameResult, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		.transition(ldrPostProcessingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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

