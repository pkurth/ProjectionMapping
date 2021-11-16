#include "pch.h"
#include "tracking.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"
#include "dx/dx_profiling.h"
#include "dx/dx_barrier_batcher.h"
#include "rendering/material.h"
#include "rendering/render_command.h"
#include "rendering/render_utils.h"

#include "tracking_rs.hlsli"

static const DXGI_FORMAT trackingDepthFormat = DXGI_FORMAT_R16_UINT;
static const DXGI_FORMAT trackingColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

static dx_pipeline createCorrespondencesPipeline;
static dx_pipeline visualizeDepthPipeline;



struct visualize_depth_material
{
	mat4 colorCameraV;
	camera_intrinsics colorCameraIntrinsics;
	camera_distortion colorCameraDistortion;
	ref<dx_texture> depthTexture;
	ref<dx_texture> unprojectTable;
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
	cb.colorCameraV = rc.material.colorCameraV;
	cb.colorCameraIntrinsics = rc.material.colorCameraIntrinsics;
	cb.colorCameraDistortion = rc.material.colorCameraDistortion;
	cb.depthScale = rc.material.depthScale;
	cb.depthWidth = rc.material.depthTexture->width;
	cb.colorWidth = rc.material.colorTexture->width;
	cb.colorHeight = rc.material.colorTexture->height;

	cl->setGraphics32BitConstants(VISUALIZE_DEPTH_RS_CB, cb);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_DEPTH_TEXTURE_AND_TABLE, 0, rc.material.depthTexture);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_DEPTH_TEXTURE_AND_TABLE, 1, rc.material.unprojectTable);
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

		visualizeDepthPipeline = createReloadablePipeline(desc, { "tracking_visualize_depth_vs", "tracking_visualize_depth_ps" });
	}

	if (!createCorrespondencesPipeline.pipeline)
	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.renderTargets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT) // Color buffer is temporary.
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

		createCorrespondencesPipeline = createReloadablePipeline(desc, { "tracking_create_correspondences_vs", "tracking_create_correspondences_ps" });
	}

	if (!camera.initializeAzure())
	{
		return;
	}

	cameraDepthTexture = createTexture(0, camera.depthSensor.width, camera.depthSensor.height, trackingDepthFormat, false, false, false, D3D12_RESOURCE_STATE_GENERIC_READ);
	uint32 requiredSize = (uint32)GetRequiredIntermediateSize(cameraDepthTexture->resource.Get(), 0, 1);
	depthUploadBuffer = createUploadBuffer(requiredSize, 1, 0);

	if (camera.colorSensor.active)
	{
		cameraColorTexture = createTexture(0, camera.colorSensor.width, camera.colorSensor.height, trackingColorFormat, false, false, false, D3D12_RESOURCE_STATE_GENERIC_READ);
		uint32 requiredSize = (uint32)GetRequiredIntermediateSize(cameraColorTexture->resource.Get(), 0, 1);
		colorUploadBuffer = createUploadBuffer(requiredSize, 1, 0);
	}

	cameraUnprojectTableTexture = createTexture(camera.depthSensor.unprojectTable, camera.depthSensor.width, camera.depthSensor.height, DXGI_FORMAT_R32G32_FLOAT);


	renderedColorTexture = createTexture(0, camera.depthSensor.width, camera.depthSensor.height, DXGI_FORMAT_R8G8B8A8_UNORM, false, true);
	renderedDepthTexture = createDepthTexture(camera.depthSensor.width, camera.depthSensor.height, DXGI_FORMAT_D32_FLOAT);
}

void depth_tracker::trackObject(scene_entity entity)
{
	trackedEntity = entity;
}

void depth_tracker::update()
{
	rgbd_frame frame;
	if (camera.getFrame(frame, 0))
	{
		dx_command_list* cl = dxContext.getFreeRenderCommandList();

		{
			PROFILE_ALL(cl, "Tracking");

			if (camera.depthSensor.active)
			{
				PROFILE_ALL(cl, "Upload depth image");

				uint32 formatSize = getFormatSize(trackingDepthFormat);

				D3D12_SUBRESOURCE_DATA subresource;
				subresource.RowPitch = camera.depthSensor.width * formatSize;
				subresource.SlicePitch = camera.depthSensor.width * camera.depthSensor.height * formatSize;
				subresource.pData = frame.depth;

				cl->transitionBarrier(cameraDepthTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
				UpdateSubresources<1>(cl->commandList.Get(), cameraDepthTexture->resource.Get(), depthUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
				cl->transitionBarrier(cameraDepthTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}

			if (camera.colorSensor.active)
			{
				PROFILE_ALL(cl, "Upload color image");

				uint32 formatSize = getFormatSize(trackingColorFormat);

				D3D12_SUBRESOURCE_DATA subresource;
				subresource.RowPitch = camera.colorSensor.width * formatSize;
				subresource.SlicePitch = camera.colorSensor.width * camera.colorSensor.height * formatSize;
				subresource.pData = frame.color;

				cl->transitionBarrier(cameraColorTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
				UpdateSubresources<1>(cl->commandList.Get(), cameraColorTexture->resource.Get(), colorUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
				cl->transitionBarrier(cameraColorTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
			}

			if (trackedEntity)
			{
				PROFILE_ALL(cl, "Create correspondences");


				cl->transitionBarrier(renderedColorTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

				cl->clearRTV(renderedColorTexture, 0.f, 0.f, 0.f, 0.f);
				cl->clearDepth(renderedDepthTexture);

				cl->setPipelineState(*createCorrespondencesPipeline.pipeline);
				cl->setGraphicsRootSignature(*createCorrespondencesPipeline.rootSignature);
				cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				auto& rasterComponent = trackedEntity.getComponent<raster_component>();
				auto& transform = trackedEntity.getComponent<transform_component>();

				dx_render_target renderTarget({ renderedColorTexture }, renderedDepthTexture);
				cl->setRenderTarget(renderTarget);
				cl->setViewport(renderTarget.viewport);

				cl->setVertexBuffer(0, rasterComponent.mesh->mesh.vertexBuffer.positions);
				cl->setVertexBuffer(1, rasterComponent.mesh->mesh.vertexBuffer.others);
				cl->setIndexBuffer(rasterComponent.mesh->mesh.indexBuffer);

				create_correspondences_cb cb;
				cb.intrinsics = camera.depthSensor.intrinsics;
				cb.distortion = camera.depthSensor.distortion;
				cb.width = camera.depthSensor.width;
				cb.height = camera.depthSensor.height;
				cb.m = createViewMatrix(camera.depthSensor.position, camera.depthSensor.rotation) * trsToMat4(transform); // TODO: Global camera position.

				cl->setGraphics32BitConstants(CREATE_CORRESPONDENCES_RS_CB, cb);

				for (auto& submesh : rasterComponent.mesh->submeshes)
				{
					cl->drawIndexed(submesh.info.numIndices, 1, submesh.info.firstIndex, submesh.info.baseVertex, 0);
				}

				cl->transitionBarrier(renderedColorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
			}

		}
		dxContext.executeCommandList(cl);

		camera.releaseFrame(frame);
	}
}

void depth_tracker::visualizeDepth(ldr_render_pass* renderPass)
{
	auto& c = camera.colorSensor;
	mat4 colorCameraV = createViewMatrix(c.position, c.rotation);
	renderPass->renderObject<visualize_depth_pipeline>(mat4::identity, {}, {}, {}, 
		visualize_depth_material{ colorCameraV, c.intrinsics, c.distortion, cameraDepthTexture, cameraUnprojectTableTexture, cameraColorTexture, camera.depthScale });
}


