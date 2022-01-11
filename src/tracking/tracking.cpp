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



static const char* trackingDirectionNames[] =
{
	"Camera to render",
	"Render to camera",
};

static const char* rotationRepresentationNames[] =
{
	"Euler",
	"Lie",
};

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
	DX_PROFILE_BLOCK(cl, "Render depth camera image");

	visualize_depth_cb cb;
	cb.vp = viewProj * rc.transform;
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




static void initializePipelines()
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
				.renderTargets(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D16_UNORM) // Color buffer is for debug window only.
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
}

depth_tracker::depth_tracker()
{
	initializePipelines();

	initialize(rgbd_camera_type_realsense, 0);
}

void depth_tracker::initialize(rgbd_camera_type cameraType, uint32 deviceIndex)
{
	if (!camera.initializeAs(cameraType, deviceIndex))
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

	ataReadbackBuffer = createReadbackBuffer(sizeof(tracking_ata_atb), NUM_BUFFERED_FRAMES * 2);
}

struct vec6
{
	float m[6];

	vec6() {}
	vec6(tracking_atb v)
	{
		memcpy(m, v.m, sizeof(float) * 6);
	}
	vec6(float a, float b, float c, float d, float e, float f)
	{
		m[0] = a; m[1] = b; m[2] = c; m[3] = d; m[4] = e; m[5] = f;
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

struct rotation_translation
{
	mat3 rotation;
	vec3 translation;
};

static rotation_translation lieExp(vec6 lie)
{
	vec3 u(lie.m[3], lie.m[4], lie.m[5]);
	vec3 w(lie.m[0], lie.m[1], lie.m[2]);

	float theta_2 = dot(w, w);

	float a, b, c;
	if (theta_2 < 1e-6f)
	{
		// Use Tailor expansion.
		a = 1.f + theta_2 * (-1.f / 6.f + theta_2 * (1.f / 120.f - theta_2 / 5040.f));
		b = 0.5f + theta_2 * (-1.f / 24.f + theta_2 * (1.f / 720.f - theta_2 / 40320.f));
		c = 1.f / 6.f + theta_2 * (-1.f / 120.f + theta_2 * (1.f / 5040.f - theta_2 / 362880.f));
	}
	else
	{
		float theta = sqrt(theta_2);
		a = sin(theta) / theta;
		b = (1.f - cos(theta)) / theta_2;
		c = (1.f - a) / theta_2;
	}

	mat3 w_x = getSkewMatrix(w);
	mat3 w_x_2 = w_x * w_x;

	mat3 R = mat3::identity + a * w_x + b * w_x_2;
	vec3 t = (mat3::identity + b * w_x + c * w_x_2) * u;

	return { R, t };
}

static vec6 lieLog(rotation_translation Rt)
{
	float theta = acos(0.5f * (trace(Rt.rotation) - 1.f));
	float theta_2 = theta * theta;

	float a, b;
	if (theta_2 < 1e-6f)
	{
		a = 1.f + theta_2 * (-1.f / 6.f + theta_2 * (1.f / 120.f - theta_2 / 5040.f));
		b = 0.5f + theta_2 * (-1.f / 24.f + theta_2 * (1.f / 720.f - theta_2 / 40320.f));
	}
	else
	{
		a = sin(theta) / theta;
		b = (1.f - cos(theta)) / theta_2;
	}
	
	mat3 w_x = (0.5f / a) * (Rt.rotation - transpose(Rt.rotation));
	vec3 w(w_x.m21, w_x.m02, w_x.m10);
	mat3 w_x_2 = w_x * w_x;
	mat3 V_inv = mat3::identity - 0.5f * w_x;
	if (theta_2 > 0.f)
	{
		V_inv += 1.f / theta_2 * (1.f - 0.5f * a / b) * w_x_2;
	}

	vec3 u = V_inv * Rt.translation;

	return vec6(w.x, w.y, w.z, u.x, u.y, u.z);
}

static rotation_translation smoothDeltaTransform(vec6 delta, float t)
{
	return lieExp(delta * t);
}

static rotation_translation smoothDeltaTransform(rotation_translation delta, float t)
{
	// https://www.geometrictools.com/Documentation/InterpolationRigidMotions.pdf
	return lieExp(lieLog(delta) * t);
}

struct solver_result
{
	vec6 x;
	depth_tracker::solver_stats stats;
};

static solver_result solve(const tracking_ata& A, const tracking_atb& b, uint32 maxNumIterations = 20)
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

	return { x, { rdotr, k } };
}

static rotation_translation eulerUpdate(vec6 x, float smoothing)
{
	float alpha = x.m[0];
	float beta = x.m[1];
	float gamma = x.m[2];

	float sinAlpha = sin(alpha);
	float cosAlpha = cos(alpha);
	float sinBeta = sin(beta);
	float cosBeta = cos(beta);
	float sinGamma = sin(gamma);
	float cosGamma = cos(gamma);

	rotation_translation rt;

	rt.rotation.m00 = cosGamma * cosBeta;
	rt.rotation.m01 = -sinGamma * cosAlpha + cosGamma * sinBeta * sinAlpha;
	rt.rotation.m02 = sinGamma * sinAlpha + cosGamma * sinBeta * cosAlpha;
	rt.rotation.m10 = sinGamma * cosBeta;
	rt.rotation.m11 = cosGamma * cosAlpha + sinGamma * sinBeta * sinAlpha;
	rt.rotation.m12 = -cosGamma * sinAlpha + sinGamma * sinBeta * cosAlpha;
	rt.rotation.m20 = -sinBeta;
	rt.rotation.m21 = cosBeta * sinAlpha;
	rt.rotation.m22 = cosBeta * cosAlpha;

	rt.translation.x = x.m[3];
	rt.translation.y = x.m[4];
	rt.translation.z = x.m[5];

	return smoothDeltaTransform(rt, 1.f - smoothing);
}

static rotation_translation lieUpdate(vec6 x, float smoothing)
{
	return smoothDeltaTransform(x, 1.f - smoothing);
}

tracker_ui_interaction depth_tracker::drawSettings()
{
	tracker_ui_interaction result = tracker_ui_no_interaction;

	if (cameraInitialized())
	{
		if (ImGui::BeginProperties())
		{
			bool valid = trackedEntity;
			if (ImGui::PropertyDisableableButton("Entity", ICON_FA_CUBE, valid, valid ? trackedEntity.getComponent<tag_component>().name : "No entity set"))
			{
				result = tracker_ui_select_tracked_entity;
			}
			ImGui::PropertyDisableableCheckbox("Tracking", tracking, valid);
			ImGui::PropertySlider("Position threshold", positionThreshold, 0.f, 0.5f);
			ImGui::PropertySliderAngle("Normal angle threshold", angleThreshold, 0.f, 90.f);
			ImGui::PropertySlider("Smoothing (higher is smoother)", smoothing);
			ImGui::PropertyInput("Min number of correspondences", minNumCorrespondences);

			ImGui::PropertyCheckbox("Visualize depth", showDepth);

			ImGui::PropertyDropdown("Direction", trackingDirectionNames, 2, (uint32&)trackingDirection);
			ImGui::PropertyDropdown("Rotation representation", rotationRepresentationNames, 2, (uint32&)rotationRepresentation);

			if (tracking)
			{
				ImGui::Separator();
				ImGui::PropertyValue("Number of correspondences", numCorrespondences);
				//ImGui::PropertyValue("Current error", stats.error, "%.8f");
				ImGui::PropertyValue("CG iterations", stats.numCGIterations);
			}

			ImGui::EndProperties();
		}

		ImGui::Image(renderedColorTexture);
	}
	else
	{
		ImGui::Text("Tracking disabled, because camera is not initialized.");
		ImGui::Separator();

		if (ImGui::BeginProperties())
		{
			uint32 index = -1;

			if (ImGui::PropertyButton("Refresh", ICON_FA_REDO_ALT))
			{
				rgbd_camera::enumerate();
			}

			bool changed = ImGui::PropertyDropdown("Available cameras", [](uint32 index, void* data) -> const char*
				{
					auto& cameras = *(std::vector<rgbd_camera_info>*)data;

					if (index == -1) { return "--"; }
					if (index >= cameras.size()) { return 0; }
					return cameras[index].description.c_str();
				}, index, &rgbd_camera::allConnectedRGBDCameras);

			if (changed)
			{
				initialize(rgbd_camera::allConnectedRGBDCameras[index].type, rgbd_camera::allConnectedRGBDCameras[index].deviceIndex);
			}

			ImGui::EndProperties();
		}
	}

	return result;
}

void depth_tracker::update()
{
	numCorrespondences = 0;

	if (cameraInitialized())
	{
		dx_command_list* cl = dxContext.getFreeRenderCommandList();

		{
			PROFILE_ALL(cl, "Tracking");


			// Upload new frame if available.
			rgbd_frame frame;
			if (camera.getFrame(frame, 0))
			{
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

				camera.releaseFrame(frame);
			}


			// If entity has been deleted, don't track it anymore.
			if (trackedEntity && !trackedEntity.registry->valid(trackedEntity.handle))
			{
				trackedEntity = {};
			}

			if (!trackedEntity)
			{
				tracking = false;
			}


			if (tracking)
			{
				CPU_PROFILE_BLOCK("Process last tracking result");

				tracking_indirect* mappedIndirect = (tracking_indirect*)mapBuffer(icpDispatchReadbackBuffer, true, map_range{ dxContext.bufferedFrameID, 1 });
				tracking_indirect indirect = mappedIndirect[dxContext.bufferedFrameID];
				unmapBuffer(icpDispatchReadbackBuffer, false);

				//std::cout << indirect.counter << " " << indirect.initialICP.ThreadGroupCountX << " " << indirect.reduce0.ThreadGroupCountX << " " << indirect.reduce1.ThreadGroupCountX << '\n';
				bool correspondencesValid = indirect.initialICP.ThreadGroupCountX > 0;
				numCorrespondences = indirect.counter;

				if (correspondencesValid)
				{
					// Due to the flip-flop reduction (below), we have to figure out in which buffer the final result has been written.
					uint32 ataBufferIndex = 0;
					if (indirect.reduce1.ThreadGroupCountX == 0) { ataBufferIndex = 1; }
					if (indirect.reduce0.ThreadGroupCountX == 0) { ataBufferIndex = 0; }

					tracking_ata_atb* mapped = (tracking_ata_atb*)mapBuffer(ataReadbackBuffer, true, map_range{ 2 * dxContext.bufferedFrameID, 2 });
					tracking_ata ata = mapped[2 * dxContext.bufferedFrameID + ataBufferIndex].ata;
					tracking_atb atb = mapped[2 * dxContext.bufferedFrameID + ataBufferIndex].atb;
					unmapBuffer(ataReadbackBuffer, false);

					solver_result result = solve(ata, atb);
					stats = result.stats;

					rotation_translation delta;
					if (rotationRepresentation == tracking_rotation_representation_euler)
					{
						delta = eulerUpdate(result.x, smoothing);
					}
					else
					{
						assert(rotationRepresentation == tracking_rotation_representation_lie);
						delta = lieUpdate(result.x, smoothing);
					}


					quat rotation = mat3ToQuaternion(delta.rotation);
					vec3 translation = delta.translation;

					if (trackingDirection == tracking_direction_camera_to_render)
					{
						rotation = conjugate(rotation);
						translation = -(rotation * translation);
					}

					rotation = camera.depthSensor.rotation * rotation * conjugate(camera.depthSensor.rotation);
					translation = camera.depthSensor.rotation * translation;

					rotation = globalCameraRotation * rotation * conjugate(globalCameraRotation);
					translation = globalCameraRotation * translation;

					if (trackedEntity)
					{
						transform_component& transform = trackedEntity.getComponent<transform_component>();
						transform.rotation = rotation * transform.rotation;
						transform.position = rotation * transform.position + translation;
					}
				}
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
				vscb.m = 
					createViewMatrix(camera.depthSensor.position, camera.depthSensor.rotation) 
					* createViewMatrix(globalCameraPosition, globalCameraRotation)
					* trsToMat4(transform);
				vscb.p = createPerspectiveProjectionMatrix((float)camera.depthSensor.width, (float)camera.depthSensor.height, i.fx, i.fy, i.cx, i.cy, 0.1f, -1.f);


				{
					PROFILE_ALL(cl, "Depth pre-pass");

					cl->setPipelineState(*createCorrespondencesDepthOnlyPipeline.pipeline);
					cl->setGraphicsRootSignature(*createCorrespondencesDepthOnlyPipeline.rootSignature);

					auto renderTarget = dx_render_target(renderedDepthTexture->width, renderedDepthTexture->height)
						.depthAttachment(renderedDepthTexture);

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

					auto renderTarget = dx_render_target(renderedDepthTexture->width, renderedDepthTexture->height)
						.colorAttachment(renderedColorTexture)
						.depthAttachment(renderedDepthTexture);

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

					cl->setCompute32BitConstants(PREPARE_DISPATCH_RS_CB, minNumCorrespondences);
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

						cl->dispatchIndirect(1, icpDispatchBuffer, offsetof(tracking_indirect, initialICP));

						barrier_batcher(cl)
							//.uav(ataBuffer0)
							.transition(ataBuffer0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
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

							cl->dispatchIndirect(1, icpDispatchBuffer, offsetof(tracking_indirect, reduce0));

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

							cl->dispatchIndirect(1, icpDispatchBuffer, offsetof(tracking_indirect, reduce1));

							barrier_batcher(cl)
								//.uav(ataBuffer0)
								.transition(ataBuffer0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);	// This is next copied to the CPU.
						}
					}
				}


				cl->copyBufferRegionToBuffer(ataBuffer0, ataReadbackBuffer, 0, 2 * dxContext.bufferedFrameID + 0, 1);
				cl->copyBufferRegionToBuffer(ataBuffer1, ataReadbackBuffer, 0, 2 * dxContext.bufferedFrameID + 1, 1);

				barrier_batcher(cl)
					.transition(correspondenceBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
					.transition(ataBuffer0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
					.transition(ataBuffer1, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				cl->copyBufferRegionToBuffer(icpDispatchBuffer, icpDispatchReadbackBuffer, 0, dxContext.bufferedFrameID, 1);

				cl->transitionBarrier(icpDispatchBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}
		}
		dxContext.executeCommandList(cl);

	}
}

void depth_tracker::visualizeDepth(ldr_render_pass* renderPass)
{
	if (showDepth && cameraInitialized())
	{
		auto& c = camera.colorSensor;
		mat4 colorCameraV = createViewMatrix(c.position, c.rotation); // Local.

		mat4 m = createModelMatrix(globalCameraPosition, globalCameraRotation) * createModelMatrix(camera.depthSensor.position, camera.depthSensor.rotation);

		renderPass->renderObject<visualize_depth_pipeline>(m, {}, {}, {},
			visualize_depth_material{ colorCameraV, c.intrinsics, c.distortion, cameraDepthTexture, cameraUnprojectTableTexture, cameraColorTexture, camera.depthScale });
	}
}


