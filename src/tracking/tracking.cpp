#include "pch.h"
#include "tracking.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"
#include "rendering/material.h"
#include "rendering/render_command.h"
#include "rendering/render_utils.h"

#include "tracking_rs.hlsli"

static const DXGI_FORMAT trackingDepthFormat = DXGI_FORMAT_R16_UINT;
static const DXGI_FORMAT trackingColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

static dx_pipeline visualizeDepthPipeline;



struct visualize_depth_material
{
	mat4 colorCameraVP;
	ref<dx_texture> depthTexture;
	ref<dx_texture> xyTable;
	ref<dx_texture> colorTexture;
	float depthScale;
};

struct visualize_depth_pipeline
{
	using material_t = visualize_depth_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(visualize_depth_pipeline)
{
	cl->setPipelineState(*visualizeDepthPipeline.pipeline);
	cl->setGraphicsRootSignature(*visualizeDepthPipeline.rootSignature);
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
}

PIPELINE_RENDER_IMPL(visualize_depth_pipeline)
{
	visualize_depth_cb cb;
	cb.vp = viewProj;
	cb.colorCameraVP = rc.material.colorCameraVP;
	cb.depthScale = rc.material.depthScale;
	cb.width = rc.material.depthTexture->height;

	cl->setGraphics32BitConstants(VISUALIZE_DEPTH_RS_CB, cb);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_DEPTH_TEXTURE_AND_TABLE, 0, rc.material.depthTexture);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_DEPTH_TEXTURE_AND_TABLE, 1, rc.material.xyTable);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_COLOR_TEXTURE, 0, rc.material.colorTexture);
	cl->draw(rc.material.depthTexture->width * rc.material.depthTexture->height, 1, 0, 0);
}







depth_tracker::depth_tracker()
{
	if (!visualizeDepthPipeline.pipeline)
	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.renderTargets(ldrFormat, depthStencilFormat)
			.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);

		visualizeDepthPipeline = createReloadablePipeline(desc, { "visualize_depth_vs", "visualize_depth_ps" });
	}

	if (!camera.initializeAzure())
	{
		return;
	}

	cameraDepthTexture = createTexture(0, camera.depthSensor.width, camera.depthSensor.height, trackingDepthFormat);
	uint32 requiredSize = (uint32)GetRequiredIntermediateSize(cameraDepthTexture->resource.Get(), 0, 1);
	depthUploadBuffer = createUploadBuffer(requiredSize, 1, 0);

	if (camera.colorSensor.active)
	{
		cameraColorTexture = createTexture(0, camera.colorSensor.width, camera.colorSensor.height, trackingColorFormat);
		uint32 requiredSize = (uint32)GetRequiredIntermediateSize(cameraColorTexture->resource.Get(), 0, 1);
		colorUploadBuffer = createUploadBuffer(requiredSize, 1, 0);
	}

	cameraXYTableTexture = createTexture(camera.depthSensor.xyTable, camera.depthSensor.width, camera.depthSensor.height, DXGI_FORMAT_R32G32_FLOAT);
}

void depth_tracker::update()
{
	rgbd_frame frame;
	if (camera.getFrame(frame, 0))
	{
		dx_command_list* cl = dxContext.getFreeCopyCommandList();

		if (camera.depthSensor.active)
		{
			uint32 formatSize = getFormatSize(trackingDepthFormat);

			D3D12_SUBRESOURCE_DATA subresource;
			subresource.RowPitch = camera.depthSensor.width * formatSize;
			subresource.SlicePitch = camera.depthSensor.width * camera.depthSensor.height * formatSize;
			subresource.pData = frame.depth;

			cl->transitionBarrier(cameraDepthTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			UpdateSubresources<1>(cl->commandList.Get(), cameraDepthTexture->resource.Get(), depthUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
		}

		if (camera.colorSensor.active)
		{
			uint32 formatSize = getFormatSize(trackingColorFormat);

			D3D12_SUBRESOURCE_DATA subresource;
			subresource.RowPitch = camera.colorSensor.width * formatSize;
			subresource.SlicePitch = camera.colorSensor.width * camera.colorSensor.height * formatSize;
			subresource.pData = frame.color;

			cl->transitionBarrier(cameraColorTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			UpdateSubresources<1>(cl->commandList.Get(), cameraColorTexture->resource.Get(), colorUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
		}

		dxContext.executeCommandList(cl);

		camera.releaseFrame(frame);
	}
}

void depth_tracker::visualizeDepth(ldr_render_pass* renderPass)
{
	auto& c = camera.colorSensor;
	mat4 colorCameraVP = createPerspectiveProjectionMatrix((float)c.width, (float)c.height, c.intrinsics.fx, c.intrinsics.fy, c.intrinsics.cx, c.intrinsics.cy, 0.01f, -1.f) 
		* createViewMatrix(c.position, c.rotation);
	renderPass->renderObject<visualize_depth_pipeline>(mat4::identity, {}, {}, {}, visualize_depth_material{ colorCameraVP, cameraDepthTexture, cameraXYTableTexture, cameraColorTexture, camera.depthScale });
}


