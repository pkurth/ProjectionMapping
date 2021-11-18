#include "pch.h"
#include "tracking.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"
#include "dx/dx_profiling.h"
#include "dx/dx_barrier_batcher.h"
#include "rendering/material.h"
#include "rendering/render_command.h"
#include "rendering/render_utils.h"
#include "rendering/render_resources.h"
#include "core/imgui.h"

#include "tracking_rs.hlsli"

static const DXGI_FORMAT trackingDepthFormat = DXGI_FORMAT_R16_UINT;
static const DXGI_FORMAT trackingColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

static dx_pipeline createCorrespondencesDepthOnlyPipeline;
static dx_pipeline createCorrespondencesPipeline;
static dx_pipeline prepareDispatchPipeline;
static dx_pipeline icpPipeline;
static dx_pipeline icpReducePipeline;

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
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_TEXTURES, 0, rc.material.depthTexture);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_TEXTURES, 1, rc.material.unprojectTable);
	cl->setDescriptorHeapSRV(VISUALIZE_DEPTH_RS_TEXTURES, 2, rc.material.colorTexture);
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
		{
			auto desc = CREATE_GRAPHICS_PIPELINE
				.renderTargets(0, 0, DXGI_FORMAT_D16_UNORM)
				.inputLayout(inputLayout_position_uv_normal_tangent)
				.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

			createCorrespondencesDepthOnlyPipeline = createReloadablePipeline(desc, { "tracking_create_correspondences_vs" }, rs_in_vertex_shader);
		}

		{
			auto desc = CREATE_GRAPHICS_PIPELINE
				.renderTargets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D16_UNORM) // Color buffer is temporary.
				.inputLayout(inputLayout_position_uv_normal_tangent)
				.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
				.depthSettings(true, false, D3D12_COMPARISON_FUNC_EQUAL);

			createCorrespondencesPipeline = createReloadablePipeline(desc, { "tracking_create_correspondences_vs", "tracking_create_correspondences_ps" });
		}
	}

	if (!prepareDispatchPipeline.pipeline)
	{
		prepareDispatchPipeline = createReloadablePipeline("tracking_prepare_dispatch_cs");
	}
	if (!icpPipeline.pipeline)
	{
		icpPipeline = createReloadablePipeline("tracking_icp_cs");
	}
	if (!icpReducePipeline.pipeline)
	{
		icpReducePipeline = createReloadablePipeline("tracking_icp_reduce_cs");
	}

	if (!camera.initializeRealsense())
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
	renderedDepthTexture = createDepthTexture(camera.depthSensor.width, camera.depthSensor.height, DXGI_FORMAT_D16_UNORM);

	icpDispatchBuffer = createBuffer(sizeof(tracking_indirect), 1, 0, true, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	correspondenceBuffer = createBuffer(sizeof(tracking_correspondence), camera.depthSensor.width * camera.depthSensor.height, 0, true, false, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	icpDispatchReadbackBuffer = createReadbackBuffer(sizeof(tracking_indirect), NUM_BUFFERED_FRAMES);

	uint32 maxNumICPThreadGroups = bucketize(camera.depthSensor.width * camera.depthSensor.height, TRACKING_ICP_BLOCK_SIZE);
	ataBuffer0 = createBuffer(sizeof(tracking_ata_atb), maxNumICPThreadGroups, 0, true, false, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	uint32 maxNumReduceThreadGroups = bucketize(maxNumICPThreadGroups, TRACKING_ICP_BLOCK_SIZE);
	ataBuffer1 = createBuffer(sizeof(tracking_ata_atb), maxNumReduceThreadGroups, 0, true, false, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	ataReadbackBuffer = createReadbackBuffer(sizeof(tracking_ata_atb), NUM_BUFFERED_FRAMES);
}

void depth_tracker::trackObject(scene_entity entity)
{
	trackedEntity = entity;
}

#if 0
union mat6
{
	struct
	{
		float
			m00, m10, m20, m30, m40, m50,
			m01, m11, m21, m31, m41, m51,
			m02, m12, m22, m32, m42, m52,
			m03, m13, m23, m33, m43, m53,
			m04, m14, m24, m34, m44, m54,
			m05, m15, m25, m35, m45, m55;
	};
	float m[36];
};

inline std::ostream& operator<<(std::ostream& s, const mat6& m)
{
	s << "[" << m.m00 << ", " << m.m01 << ", " << m.m02 << ", " << m.m03 << ", " << m.m04 << ", " << m.m05 << "]\n";
	s << "[" << m.m10 << ", " << m.m11 << ", " << m.m12 << ", " << m.m13 << ", " << m.m14 << ", " << m.m15 << "]\n";
	s << "[" << m.m20 << ", " << m.m21 << ", " << m.m22 << ", " << m.m23 << ", " << m.m24 << ", " << m.m25 << "]\n";
	s << "[" << m.m30 << ", " << m.m31 << ", " << m.m32 << ", " << m.m33 << ", " << m.m34 << ", " << m.m35 << "]\n";
	s << "[" << m.m40 << ", " << m.m41 << ", " << m.m42 << ", " << m.m43 << ", " << m.m44 << ", " << m.m45 << "]\n";
	s << "[" << m.m50 << ", " << m.m51 << ", " << m.m52 << ", " << m.m53 << ", " << m.m54 << ", " << m.m55 << "]";
	return s;
}

static mat6 triangleATAToMat6(const tracking_ata& ata)
{
	mat6 result;

	result.m00 = ata.m[ata_m00];
	result.m01 = result.m10 = ata.m[ata_m01];
	result.m02 = result.m20 = ata.m[ata_m02];
	result.m03 = result.m30 = ata.m[ata_m03];
	result.m04 = result.m40 = ata.m[ata_m04];
	result.m05 = result.m50 = ata.m[ata_m05];

	result.m11 = ata.m[ata_m11];
	result.m12 = result.m21 = ata.m[ata_m12];
	result.m13 = result.m31 = ata.m[ata_m13];
	result.m14 = result.m41 = ata.m[ata_m14];
	result.m15 = result.m51 = ata.m[ata_m15];

	result.m22 = ata.m[ata_m22];
	result.m23 = result.m32 = ata.m[ata_m23];
	result.m24 = result.m42 = ata.m[ata_m24];
	result.m25 = result.m52 = ata.m[ata_m25];

	result.m33 = ata.m[ata_m33];
	result.m34 = result.m43 = ata.m[ata_m34];
	result.m35 = result.m53 = ata.m[ata_m35];

	result.m44 = ata.m[ata_m44];
	result.m45 = result.m54 = ata.m[ata_m45];

	result.m55 = ata.m[ata_m55];

	return result;
}
#endif

struct vec6
{
	float m[6];

	vec6() {}
	vec6(tracking_atb v)
	{
		memcpy(m, v.m, sizeof(float) * 6);
	}
};

static vec6 operator*(const tracking_ata& m, const vec6& v)
{
	vec6 result;
	result.m[0] = m.m[ata_m00] * v.m[0] + m.m[ata_m01] * v.m[1] + m.m[ata_m02] * v.m[2] + m.m[ata_m03] * v.m[3] + m.m[ata_m04] * v.m[4] + m.m[ata_m05] * v.m[5];
	result.m[1] = m.m[ata_m01] * v.m[0] + m.m[ata_m11] * v.m[1] + m.m[ata_m12] * v.m[2] + m.m[ata_m13] * v.m[3] + m.m[ata_m14] * v.m[4] + m.m[ata_m15] * v.m[5];
	result.m[2] = m.m[ata_m02] * v.m[0] + m.m[ata_m12] * v.m[1] + m.m[ata_m22] * v.m[2] + m.m[ata_m23] * v.m[3] + m.m[ata_m24] * v.m[4] + m.m[ata_m25] * v.m[5];
	result.m[3] = m.m[ata_m03] * v.m[0] + m.m[ata_m13] * v.m[1] + m.m[ata_m23] * v.m[2] + m.m[ata_m33] * v.m[3] + m.m[ata_m34] * v.m[4] + m.m[ata_m35] * v.m[5];
	result.m[4] = m.m[ata_m04] * v.m[0] + m.m[ata_m14] * v.m[1] + m.m[ata_m24] * v.m[2] + m.m[ata_m34] * v.m[3] + m.m[ata_m44] * v.m[4] + m.m[ata_m45] * v.m[5];
	result.m[5] = m.m[ata_m05] * v.m[0] + m.m[ata_m15] * v.m[1] + m.m[ata_m25] * v.m[2] + m.m[ata_m35] * v.m[3] + m.m[ata_m45] * v.m[4] + m.m[ata_m55] * v.m[5];
	return result;
}

static vec6 operator+(const vec6& a, const vec6& b)
{
	vec6 result;
	for (uint32 i = 0; i < 6; ++i)
	{
		result.m[i] = a.m[i] + b.m[i];
	}
	return result;
}

static vec6 operator-(const vec6& a, const vec6& b)
{
	vec6 result;
	for (uint32 i = 0; i < 6; ++i)
	{
		result.m[i] = a.m[i] - b.m[i];
	}
	return result;
}

static vec6 operator*(const vec6& a, float b)
{
	vec6 result;
	for (uint32 i = 0; i < 6; ++i)
	{
		result.m[i] = a.m[i] * b;
	}
	return result;
}

static float dot(const vec6& a, const vec6& b)
{
	float result = 0.f;
	for (uint32 i = 0; i < 6; ++i)
	{
		result += a.m[i] * b.m[i];
	}
	return result;
}

inline std::ostream& operator<<(std::ostream& s, const tracking_ata& m)
{
	s << "[" << m.m[ata_m00] << ", " << m.m[ata_m01] << ", " << m.m[ata_m02] << ", " << m.m[ata_m03] << ", " << m.m[ata_m04] << ", " << m.m[ata_m05] << "]\n";
	s << "[" << m.m[ata_m01] << ", " << m.m[ata_m11] << ", " << m.m[ata_m12] << ", " << m.m[ata_m13] << ", " << m.m[ata_m14] << ", " << m.m[ata_m15] << "]\n";
	s << "[" << m.m[ata_m02] << ", " << m.m[ata_m12] << ", " << m.m[ata_m22] << ", " << m.m[ata_m23] << ", " << m.m[ata_m24] << ", " << m.m[ata_m25] << "]\n";
	s << "[" << m.m[ata_m03] << ", " << m.m[ata_m13] << ", " << m.m[ata_m23] << ", " << m.m[ata_m33] << ", " << m.m[ata_m34] << ", " << m.m[ata_m35] << "]\n";
	s << "[" << m.m[ata_m04] << ", " << m.m[ata_m14] << ", " << m.m[ata_m24] << ", " << m.m[ata_m34] << ", " << m.m[ata_m44] << ", " << m.m[ata_m45] << "]\n";
	s << "[" << m.m[ata_m05] << ", " << m.m[ata_m15] << ", " << m.m[ata_m25] << ", " << m.m[ata_m35] << ", " << m.m[ata_m45] << ", " << m.m[ata_m55] << "]";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, tracking_atb v)
{
	s << "[" << v.m[0] << ", " << v.m[1] << ", " << v.m[2] << ", " << v.m[3] << ", " << v.m[4] << ", " << v.m[5] << "]";
	return s;
}

inline std::ostream& operator<<(std::ostream& s, vec6 v)
{
	s << "[" << v.m[0] << ", " << v.m[1] << ", " << v.m[2] << ", " << v.m[3] << ", " << v.m[4] << ", " << v.m[5] << "]";
	return s;
}

struct tracking_result
{
	quat rotation;
	vec3 translation;
	float error;
	uint32 numIterations;
};

static tracking_result solve(const tracking_ata& A, const tracking_atb& b, uint32 maxNumIterations = 20)
{
	vec6 x;
	memset(&x, 0, sizeof(x));

	vec6 r = b - A * x;
	vec6 p = r;

	float rdotr = dot(r, r);

	uint32 k;
	for (k = 0; k < maxNumIterations; ++k)
	{
		vec6 Ap = A * p;
		float alpha = rdotr / dot(p, Ap);
		x = x + p * alpha;
		r = r - Ap * alpha;

		float oldrdotr = rdotr;
		rdotr = dot(r, r);
		if (rdotr < 1e-5f)
		{
			break;
		}

		float beta = rdotr / oldrdotr;
		p = r + p * beta;
	}


	float alpha = x.m[0];
	float beta = x.m[1];
	float gamma = x.m[2];

	float sinAlpha = sin(alpha);
	float cosAlpha = cos(alpha);
	float sinBeta = sin(beta);
	float cosBeta = cos(beta);
	float sinGamma = sin(gamma);
	float cosGamma = cos(gamma);

	mat3 R;
	R.m00 = cosGamma * cosBeta;
	R.m01 = -sinGamma * cosAlpha + cosGamma * sinBeta * sinAlpha;
	R.m02 = sinGamma * sinAlpha + cosGamma * sinBeta * cosAlpha;
	R.m10 = sinGamma * cosBeta;
	R.m11 = cosGamma * cosAlpha + sinGamma * sinBeta * sinAlpha;
	R.m12 = -cosGamma * sinAlpha + sinGamma * sinBeta * cosAlpha;
	R.m20 = -sinBeta;
	R.m21 = cosBeta * sinAlpha;
	R.m22 = cosBeta * cosAlpha;

	vec3 t;
	t.x = x.m[3];
	t.y = x.m[4];
	t.z = x.m[5];

	return { mat3ToQuaternion(R), t, rdotr, k };
}

void depth_tracker::update()
{
	static tracking_result result = {};

	if (ImGui::Begin("Settings"))
	{
		if (ImGui::BeginTree("Tracker"))
		{
			if (ImGui::BeginProperties())
			{
				ImGui::PropertyCheckbox("Tracking", tracking);
				ImGui::PropertySlider("Position threshold", positionThreshold, 0.f, 0.5f);
				ImGui::PropertySliderAngle("Angle threshold", angleThreshold, 0.f, 90.f);

				ImGui::PropertyValue("Current error", result.error, "%.8f");
				ImGui::PropertyValue("Iterations", result.numIterations);
				ImGui::PropertyValue("Translation", result.translation);
				ImGui::PropertyValue("Rotation", result.rotation);
				ImGui::EndProperties();
			}

			ImGui::EndTree();
		}
	}
	ImGui::End();

	rgbd_frame frame;
	if (camera.getFrame(frame, 0))
	{
		dx_command_list* cl = dxContext.getFreeRenderCommandList();

		static uint32 read = 0;

		{
			PROFILE_ALL(cl, "Tracking");

			if (tracking)
			{
				bool correspondencesValid;

				{
					tracking_indirect* mapped = (tracking_indirect*)mapBuffer(icpDispatchReadbackBuffer, true, map_range{ read, 1 });
					tracking_indirect indirect = mapped[read];
					unmapBuffer(icpDispatchReadbackBuffer, false);

					//std::cout << indirect.counter << " " << indirect.initialICP.ThreadGroupCountX << " " << indirect.reduce0.ThreadGroupCountX << " " << indirect.reduce1.ThreadGroupCountX << '\n';
					correspondencesValid = indirect.initialICP.ThreadGroupCountX > 0;
				}

				if (correspondencesValid)
				{
					tracking_ata_atb* mapped = (tracking_ata_atb*)mapBuffer(ataReadbackBuffer, true, map_range{ read, 1 });
					tracking_ata ata = mapped[read].ata;
					tracking_atb atb = mapped[read].atb;
					unmapBuffer(ataReadbackBuffer, false);


					result = solve(ata, atb);

					//std::cout << ata << '\n';
					//std::cout << "------\n";
					//std::cout << atb << '\n';
					//std::cout << "------\n";
					//std::cout << result.rotation << " " << result.translation << '\n';
					//std::cout << "------\n";
					//std::cout << "------\n";

					result.rotation = conjugate(result.rotation);
					result.translation = -(result.rotation * result.translation);

					if (trackedEntity)
					{
						transform_component& transform = trackedEntity.getComponent<transform_component>();
						quat newRotation = result.rotation * transform.rotation;
						vec3 newPosition = result.rotation * transform.position + result.translation;

						transform.rotation = slerp(transform.rotation, newRotation, 0.1f);
						transform.position = lerp(transform.position, newPosition, 0.1f);
					}
				}
			}

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

				cl->clearRTV(renderedColorTexture, 0.f, 0.f, 0.f, 1.f);
				cl->clearDepth(renderedDepthTexture);

				cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


				auto& rasterComponent = trackedEntity.getComponent<raster_component>();
				auto& transform = trackedEntity.getComponent<transform_component>();

				auto& i = camera.depthSensor.intrinsics;
				create_correspondences_vs_cb vscb;
				vscb.distortion = camera.depthSensor.distortion;
				vscb.m = createViewMatrix(camera.depthSensor.position, camera.depthSensor.rotation) * trsToMat4(transform); // TODO: Global camera position.
				vscb.p = createPerspectiveProjectionMatrix((float)camera.depthSensor.width, (float)camera.depthSensor.height, i.fx, i.fy, i.cx, i.cy, 0.1f, -1.f);


				{
					PROFILE_ALL(cl, "Depth pre-pass");

					cl->setPipelineState(*createCorrespondencesDepthOnlyPipeline.pipeline);
					cl->setGraphicsRootSignature(*createCorrespondencesDepthOnlyPipeline.rootSignature);

					dx_render_target renderTarget({ }, renderedDepthTexture);
					cl->setRenderTarget(renderTarget);
					cl->setViewport(renderTarget.viewport);

					cl->setVertexBuffer(0, rasterComponent.mesh->mesh.vertexBuffer.positions);
					cl->setVertexBuffer(1, rasterComponent.mesh->mesh.vertexBuffer.others);
					cl->setIndexBuffer(rasterComponent.mesh->mesh.indexBuffer);

					cl->setGraphics32BitConstants(CREATE_CORRESPONDENCES_RS_VS_CB, vscb);

					for (auto& submesh : rasterComponent.mesh->submeshes)
					{
						cl->drawIndexed(submesh.info.numIndices, 1, submesh.info.firstIndex, submesh.info.baseVertex, 0);
					}
				}


				{
					PROFILE_ALL(cl, "Collect correspondences");

					cl->clearUAV(icpDispatchBuffer);

					cl->setPipelineState(*createCorrespondencesPipeline.pipeline);
					cl->setGraphicsRootSignature(*createCorrespondencesPipeline.rootSignature);

					dx_render_target renderTarget({ renderedColorTexture }, renderedDepthTexture);
					cl->setRenderTarget(renderTarget);
					cl->setViewport(renderTarget.viewport);

					cl->setVertexBuffer(0, rasterComponent.mesh->mesh.vertexBuffer.positions);
					cl->setVertexBuffer(1, rasterComponent.mesh->mesh.vertexBuffer.others);
					cl->setIndexBuffer(rasterComponent.mesh->mesh.indexBuffer);

					create_correspondences_ps_cb pscb;
					pscb.depthScale = camera.depthScale;
					pscb.squaredPositionThreshold = positionThreshold * positionThreshold;
					pscb.cosAngleThreshold = cos(angleThreshold);
					pscb.trackingDirection = trackingDirection;

					cl->setGraphics32BitConstants(CREATE_CORRESPONDENCES_RS_VS_CB, vscb);
					cl->setGraphics32BitConstants(CREATE_CORRESPONDENCES_RS_PS_CB, pscb);
					cl->setDescriptorHeapSRV(CREATE_CORRESPONDENCES_RS_SRV_UAV, 0, cameraDepthTexture);
					cl->setDescriptorHeapSRV(CREATE_CORRESPONDENCES_RS_SRV_UAV, 1, cameraUnprojectTableTexture);
					cl->setDescriptorHeapUAV(CREATE_CORRESPONDENCES_RS_SRV_UAV, 2, correspondenceBuffer);
					cl->setDescriptorHeapUAV(CREATE_CORRESPONDENCES_RS_SRV_UAV, 3, icpDispatchBuffer);

					for (auto& submesh : rasterComponent.mesh->submeshes)
					{
						cl->drawIndexed(submesh.info.numIndices, 1, submesh.info.firstIndex, submesh.info.baseVertex, 0);
					}

					barrier_batcher(cl)
						.uav(icpDispatchBuffer)
						.transition(correspondenceBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)
						.transition(renderedColorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
				}

				{
					PROFILE_ALL(cl, "Prepare dispatch");
					
					cl->setPipelineState(*prepareDispatchPipeline.pipeline);
					cl->setComputeRootSignature(*prepareDispatchPipeline.rootSignature);

					cl->setRootComputeUAV(PREPARE_DISPATCH_RS_BUFFER, icpDispatchBuffer);
					cl->dispatch(1);

					barrier_batcher(cl)
						//.uav(icpDispatchBuffer)
						.transition(icpDispatchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
				}

				{
					PROFILE_ALL(cl, "ICP");

					{
						PROFILE_ALL(cl, "Initial");

						cl->setPipelineState(*icpPipeline.pipeline);
						cl->setComputeRootSignature(*icpPipeline.rootSignature);

						cl->setRootComputeSRV(ICP_RS_COUNTER, icpDispatchBuffer);
						cl->setRootComputeSRV(ICP_RS_CORRESPONDENCES, correspondenceBuffer);
						cl->setRootComputeUAV(ICP_RS_OUTPUT, ataBuffer0);

						cl->dispatchIndirect(1, icpDispatchBuffer, 0);

						barrier_batcher(cl)
							//.uav(ataBuffer0)
							.transition(ataBuffer0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
						cl->uavBarrier(ataBuffer0);
					}


					{
						PROFILE_ALL(cl, "Reduce");

						cl->setPipelineState(*icpReducePipeline.pipeline);
						cl->setComputeRootSignature(*icpReducePipeline.rootSignature);

						cl->setRootComputeSRV(ICP_REDUCE_RS_COUNTER, icpDispatchBuffer);

						{
							PROFILE_ALL(cl, "Reduce 0");

							cl->setCompute32BitConstants(ICP_REDUCE_RS_CB, tracking_icp_reduce_cb{ 0 });
							cl->setRootComputeSRV(ICP_REDUCE_RS_INPUT, ataBuffer0);
							cl->setRootComputeUAV(ICP_REDUCE_RS_OUTPUT, ataBuffer1);

							cl->dispatchIndirect(1, icpDispatchBuffer, sizeof(D3D12_DISPATCH_ARGUMENTS) * 1);

							barrier_batcher(cl)
								//.uav(ataBuffer1)
								.transition(ataBuffer1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)
								.transition(ataBuffer0, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						}

						{
							PROFILE_ALL(cl, "Reduce 1");

							cl->setCompute32BitConstants(ICP_REDUCE_RS_CB, tracking_icp_reduce_cb{ 1 });
							cl->setRootComputeSRV(ICP_REDUCE_RS_INPUT, ataBuffer1);
							cl->setRootComputeUAV(ICP_REDUCE_RS_OUTPUT, ataBuffer0);

							cl->dispatchIndirect(1, icpDispatchBuffer, sizeof(D3D12_DISPATCH_ARGUMENTS) * 2);

							barrier_batcher(cl)
								.uav(ataBuffer0)
								.transition(ataBuffer0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE) // This is next copied to the CPU.
								.transition(ataBuffer1, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						}
					}


					barrier_batcher(cl)
						.transition(icpDispatchBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
						.transition(correspondenceBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}


				cl->copyBufferRegionToBuffer(ataBuffer0, ataReadbackBuffer, 0, read, 1);
				cl->transitionBarrier(ataBuffer0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


				// For debug.
				cl->transitionBarrier(icpDispatchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
				cl->copyBufferRegionToBuffer(icpDispatchBuffer, icpDispatchReadbackBuffer, 0, read, 1);
				cl->transitionBarrier(icpDispatchBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}

		}
		dxContext.executeCommandList(cl);

		camera.releaseFrame(frame);

		read = 1 - read;
	}
}

void depth_tracker::visualizeDepth(ldr_render_pass* renderPass)
{
	auto& c = camera.colorSensor;
	mat4 colorCameraV = createViewMatrix(c.position, c.rotation);
	renderPass->renderObject<visualize_depth_pipeline>(mat4::identity, {}, {}, {}, 
		visualize_depth_material{ colorCameraV, c.intrinsics, c.distortion, cameraDepthTexture, cameraUnprojectTableTexture, cameraColorTexture, camera.depthScale });
}


