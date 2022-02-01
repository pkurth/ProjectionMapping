#include "pch.h"
#include "application.h"
#include "dx/dx_texture.h"
#include "core/random.h"
#include "core/color.h"
#include "core/imgui.h"
#include "core/log.h"
#include "dx/dx_context.h"
#include "dx/dx_profiling.h"
#include "core/threading.h"
#include "rendering/mesh_shader.h"
#include "rendering/shadow_map.h"
#include "rendering/shadow_map_renderer.h"
#include "rendering/debug_visualization.h"


struct raytrace_component
{
	raytracing_object_type type;
};

static raytracing_object_type defineBlasFromMesh(const ref<composite_mesh>& mesh, path_tracer& pathTracer)
{
	if (dxContext.featureSupport.raytracing())
	{
		raytracing_blas_builder blasBuilder;
		std::vector<ref<pbr_material>> raytracingMaterials;

		for (auto& sm : mesh->submeshes)
		{
			blasBuilder.push(mesh->mesh.vertexBuffer, mesh->mesh.indexBuffer, sm.info);
			raytracingMaterials.push_back(sm.material);
		}

		ref<raytracing_blas> blas = blasBuilder.finish();
		raytracing_object_type type = pathTracer.defineObjectType(blas, raytracingMaterials);
		return type;
	}
	else
	{
		return {};
	}
}

void application::loadCustomShaders()
{
	if (dxContext.featureSupport.meshShaders())
	{
		initializeMeshShader();
	}
}

void application::initialize(main_renderer* renderer, projector_manager* projectorManager, depth_tracker* tracker)
{
	this->renderer = renderer;
	this->projectorManager = projectorManager;
	this->tracker = tracker;

	if (dxContext.featureSupport.raytracing())
	{
		raytracingTLAS.initialize();
	}

	scene.camera.initializeIngame(vec3(0.f, 1.f, 5.f), quat::identity, deg2rad(70.f), 0.1f);

	editor.initialize(&scene, renderer, tracker, projectorManager);

	quat setupRotation = quat::identity;

	if (auto targetObjectMesh = loadMeshFromFile("assets/meshes/augustus.obj"))
	{
#if 1
		mat4 tracking(0.055836f, -0.997742f, -0.053892f, 0.000000f, 0.962090f, 0.068226f, -0.266327f, 0.000000f, 0.269092f, -0.036935f, 0.962616f, 0.000000f, -0.218112f, -0.047165f, -0.532164f, 1.000000f);
		tracking = transpose(tracking);
		trs transform = mat4ToTRS(tracking);
#else
		trs transform = trs::identity;
#endif

		setupRotation = rotateFromTo(transform.rotation * vec3(0.f, 1.f, 0.f), vec3(0.f, 1.f, 0.f));
		
		
		transform.rotation = setupRotation * transform.rotation;
		transform.position = setupRotation * transform.position;

		auto targetObject = scene.createEntity("Target object")
			.addComponent<transform_component>(transform)
			.addComponent<raster_component>(targetObjectMesh);

		tracker->trackedEntity = targetObject;
	}

	tracker->globalCameraRotation = setupRotation;


	editor.setEnvironment("assets/sky/sunset_in_the_chalk_quarry_4k.hdr");


	scene.sun.direction = normalize(vec3(-0.6f, -1.f, -0.3f));
	scene.sun.color = vec3(1.f, 0.93f, 0.76f);
	scene.sun.intensity = 1.f;

	scene.sun.numShadowCascades = 3;
	scene.sun.shadowDimensions = 2048;
	scene.sun.cascadeDistances = vec4(9.f, 25.f, 50.f, 10000.f);
	scene.sun.bias = vec4(0.000588f, 0.000784f, 0.000824f, 0.0035f);
	scene.sun.blendDistances = vec4(5.f, 10.f, 10.f, 10.f);
	scene.sun.stabilize = true;

	for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
	{
		pointLightBuffer[i] = createUploadBuffer(sizeof(point_light_cb), 512, 0);
		spotLightBuffer[i] = createUploadBuffer(sizeof(spot_light_cb), 512, 0);
		decalBuffer[i] = createUploadBuffer(sizeof(pbr_decal_cb), 512, 0);

		spotLightShadowInfoBuffer[i] = createUploadBuffer(sizeof(spot_shadow_info), 512, 0);
		pointLightShadowInfoBuffer[i] = createUploadBuffer(sizeof(point_shadow_info), 512, 0);

		SET_NAME(pointLightBuffer[i]->resource, "Point lights");
		SET_NAME(spotLightBuffer[i]->resource, "Spot lights");
		SET_NAME(decalBuffer[i]->resource, "Decals");

		SET_NAME(spotLightShadowInfoBuffer[i]->resource, "Spot light shadow infos");
		SET_NAME(pointLightShadowInfoBuffer[i]->resource, "Point light shadow infos");
	}


	// Dummy projectors.

#if 0
	uint32 numProjectors = 0;
	float projectorRadius = 2.3f;
	for (uint32 i = 0; i < numProjectors; ++i)
	{
		float angle = M_TAU / numProjectors * i;
		vec3 position(cos(angle) * projectorRadius, 1.f, sin(angle) * projectorRadius);
		vec3 target(0.f, 1.f, 0.f);
		quat rotation = lookAtQuaternion(normalize(target - position), vec3(0.f, 1.f, 0.f));

		scene.createEntity("Projector")
			.addComponent<position_rotation_component>(position, rotation)
			.addComponent<projector_component>();
	}
#else
	/*scene.createEntity("Projector")
		.addComponent<position_rotation_component>(vec3(0.25f, 0.1f, 0.46f), quat(vec3(0.f, 1.f, 0.f), deg2rad(20.f)))
		.addComponent<projector_component>();
	
	scene.createEntity("Projector")
		.addComponent<position_rotation_component>(vec3(-0.17f, 0.1f, 0.43f), quat(vec3(0.f, 1.f, 0.f), deg2rad(-20.f)))
		.addComponent<projector_component>();*/

	//for (uint32 i = 0; i < 9; ++i)
	//{
	//	vec3 pos(0.059607f, -0.365440f, 0.533256f);
	//	quat rotation = mat3ToQuaternion(transpose(mat3(-0.069518f, -0.967943f, -0.241359f, 0.960249f, 0.000640f, -0.279144f, 0.270350f, -0.251171f, 0.929421f)));
	//
	//	pos = setupRotation * pos;
	//	rotation = setupRotation * rotation;
	//
	//	render_camera projCamera;
	//	projCamera.initializeCalibrated(pos, rotation, 1920, 1200, camera_intrinsics{ 2717.140869f, 2714.319580f, 974.937622f, 1095.246460f }, 0.01f);
	//
	//	scene.createEntity("Projector")
	//		.addComponent<position_rotation_component>(pos, rotation)
	//		.addComponent<projector_component>(projCamera);
	//}
	//
	//{
	//	vec3 pos(0.105580f, 0.209924f, 0.440254f);
	//	quat rotation = mat3ToQuaternion(transpose(mat3(0.075530f, -0.979857f, 0.184866f, 0.981546f, 0.040397f, -0.186910f, 0.175677f, 0.195572f, 0.964826f)));
	//
	//	pos = setupRotation * pos;
	//	rotation = setupRotation * rotation;
	//
	//	render_camera projCamera;
	//	projCamera.initializeCalibrated(pos, rotation, 1920, 1200, camera_intrinsics{ 2690.001709f, 2693.022949f, 979.055237f, 991.163513f }, 0.01f);
	//
	//	scene.createEntity("Projector")
	//		.addComponent<position_rotation_component>(pos, rotation)
	//		.addComponent<projector_component>(projCamera);
	//}



#endif



	stackArena.initialize();
}

void application::resetRenderPasses()
{
	opaqueRenderPass.reset();
	transparentRenderPass.reset();
	ldrRenderPass.reset();
	sunShadowRenderPass.reset();

	for (uint32 i = 0; i < numSpotShadowRenderPasses; ++i)
	{
		spotShadowRenderPasses[i].reset();
	}

	for (uint32 i = 0; i < numPointShadowRenderPasses; ++i)
	{
		pointShadowRenderPasses[i].reset();
	}

	numSpotShadowRenderPasses = 0;
	numPointShadowRenderPasses = 0;

	projectorOpaqueRenderPass.reset();
}

void application::submitRenderPasses(uint32 numSpotLightShadowPasses, uint32 numPointLightShadowPasses)
{
	opaqueRenderPass.sort();
	transparentRenderPass.sort();
	ldrRenderPass.sort();
	projectorOpaqueRenderPass.sort();

	renderer->submitRenderPass(&opaqueRenderPass);
	renderer->submitRenderPass(&transparentRenderPass);
	renderer->submitRenderPass(&ldrRenderPass);

	shadow_map_renderer::submitRenderPass(&sunShadowRenderPass);

	for (uint32 i = 0; i < numSpotLightShadowPasses; ++i)
	{
		shadow_map_renderer::submitRenderPass(&spotShadowRenderPasses[i]);
	}

	for (uint32 i = 0; i < numPointLightShadowPasses; ++i)
	{
		shadow_map_renderer::submitRenderPass(&pointShadowRenderPasses[i]);
	}
}

void application::update(const user_input& input, float dt)
{
	CPU_PROFILE_BLOCK("App update");

	stackArena.reset();

	resetRenderPasses();

	bool objectDragged = editor.update(input, &ldrRenderPass, dt);

	scene_entity selectedEntity = editor.selectedEntity;


	scene.sun.updateMatrices(scene.camera);

	// Set global rendering stuff.
	renderer->setCamera(scene.camera);
	renderer->setSun(scene.sun);
	renderer->setEnvironment(scene.environment);



	// Update projector frusta and set render parameters.
	for (auto [entityHandle, projector, transform] : scene.group(entt::get<projector_component, position_rotation_component>).each())
	{
		render_camera camera;
		camera.initializeCalibrated(transform.position, transform.rotation, projector.width, projector.height, projector.intrinsics, 0.01f);
		camera.updateMatrices();

		renderCameraFrustum(camera, projector.frustumColor, &ldrRenderPass, 4.f);


		projector.renderer.submitRenderPass(&projectorOpaqueRenderPass);
	}


	projector_renderer::setViewerCamera(scene.camera);
	projector_renderer::setSun(scene.sun);
	projector_renderer::setEnvironment(scene.environment);



#if 1
	if (renderer->mode != renderer_mode_pathtraced)
	{
		if (dxContext.featureSupport.meshShaders())
		{
			//testRenderMeshShader(&overlayRenderPass);
		}

		thread_job_context context;

		// Update animated meshes.
		for (auto [entityHandle, anim, raster, transform] : scene.group(entt::get<animation_component, raster_component, transform_component>).each())
		{
			context.addWork([&anim = anim, mesh = raster.mesh, &transform = transform, dt]()
			{
				anim.update(mesh, dt, &transform);
			});
		}

		context.waitForWorkCompletion();


		// Render shadow maps.
		renderSunShadowMap(scene.sun, &sunShadowRenderPass, scene, objectDragged);

		uint32 numPointLights = scene.numberOfComponentsOfType<point_light_component>();
		if (numPointLights)
		{
			auto* plPtr = (point_light_cb*)mapBuffer(pointLightBuffer[dxContext.bufferedFrameID], false);
			auto* siPtr = (point_shadow_info*)mapBuffer(pointLightShadowInfoBuffer[dxContext.bufferedFrameID], false);

			for (auto [entityHandle, position, pl] : scene.group<position_component, point_light_component>().each())
			{
				point_light_cb cb;
				cb.initialize(position.position, pl.color * pl.intensity, pl.radius);

				if (pl.castsShadow)
				{
					cb.shadowInfoIndex = numPointShadowRenderPasses++;
					*siPtr++ = renderPointShadowMap(cb, (uint32)entityHandle, &pointShadowRenderPasses[cb.shadowInfoIndex], scene, objectDragged, pl.shadowMapResolution);
				}

				*plPtr++ = cb;
			}

			unmapBuffer(pointLightBuffer[dxContext.bufferedFrameID], true, { 0, numPointLights });
			unmapBuffer(pointLightShadowInfoBuffer[dxContext.bufferedFrameID], true, { 0, numPointShadowRenderPasses });

			renderer->setPointLights(pointLightBuffer[dxContext.bufferedFrameID], numPointLights, pointLightShadowInfoBuffer[dxContext.bufferedFrameID]);
			projector_renderer::setPointLights(pointLightBuffer[dxContext.bufferedFrameID], numPointLights, pointLightShadowInfoBuffer[dxContext.bufferedFrameID]);
		}

		uint32 numSpotLights = scene.numberOfComponentsOfType<spot_light_component>();
		if (numSpotLights)
		{
			auto* slPtr = (spot_light_cb*)mapBuffer(spotLightBuffer[dxContext.bufferedFrameID], false);
			auto* siPtr = (spot_shadow_info*)mapBuffer(spotLightShadowInfoBuffer[dxContext.bufferedFrameID], false);

			for (auto [entityHandle, transform, sl] : scene.group<position_rotation_component, spot_light_component>().each())
			{
				spot_light_cb cb;
				cb.initialize(transform.position, transform.rotation * vec3(0.f, 0.f, -1.f), sl.color * sl.intensity, sl.innerAngle, sl.outerAngle, sl.distance);

				if (sl.castsShadow)
				{
					cb.shadowInfoIndex = numSpotShadowRenderPasses++;
					*siPtr++ = renderSpotShadowMap(cb, (uint32)entityHandle, &spotShadowRenderPasses[cb.shadowInfoIndex], scene, objectDragged, sl.shadowMapResolution);
				}

				*slPtr++ = cb;
			}

			unmapBuffer(spotLightBuffer[dxContext.bufferedFrameID], true, { 0, numSpotLights });
			unmapBuffer(spotLightShadowInfoBuffer[dxContext.bufferedFrameID], true, { 0, numSpotShadowRenderPasses });

			renderer->setSpotLights(spotLightBuffer[dxContext.bufferedFrameID], numSpotLights, spotLightShadowInfoBuffer[dxContext.bufferedFrameID]);
			projector_renderer::setSpotLights(spotLightBuffer[dxContext.bufferedFrameID], numSpotLights, spotLightShadowInfoBuffer[dxContext.bufferedFrameID]);
		}



		// Submit render calls.
		for (auto [entityHandle, raster, transform] : scene.group(entt::get<raster_component, transform_component>).each())
		{
			const dx_mesh& mesh = raster.mesh->mesh;
			mat4 m = trsToMat4(transform);

			scene_entity entity = { entityHandle, scene };
			bool outline = selectedEntity == entity;

			dynamic_transform_component* dynamic = entity.getComponentIfExists<dynamic_transform_component>();
			mat4 lastM = dynamic ? trsToMat4(*dynamic) : m;

			if (animation_component* anim = entity.getComponentIfExists<animation_component>())
			{
				uint32 numSubmeshes = (uint32)raster.mesh->submeshes.size();

				for (uint32 i = 0; i < numSubmeshes; ++i)
				{
					submesh_info submesh = raster.mesh->submeshes[i].info;
					submesh.baseVertex -= raster.mesh->submeshes[0].info.baseVertex; // Vertex buffer from skinning already points to first vertex.

					const ref<pbr_material>& material = raster.mesh->submeshes[i].material;

					if (material->albedoTint.a < 1.f)
					{
						transparentRenderPass.renderObject(m, anim->currentVertexBuffer, mesh.indexBuffer, submesh, material);
					}
					else
					{
						opaqueRenderPass.renderAnimatedObject(m, lastM,
							anim->currentVertexBuffer, anim->prevFrameVertexBuffer, mesh.indexBuffer,
							submesh, material,
							(uint32)entityHandle);

						projectorOpaqueRenderPass.renderAnimatedObject(m, lastM,
							anim->currentVertexBuffer, anim->prevFrameVertexBuffer, mesh.indexBuffer,
							submesh, material);
					}

					if (outline)
					{
						ldrRenderPass.renderOutline(m, anim->currentVertexBuffer, mesh.indexBuffer, submesh);
					}
				}
			}
			else
			{
				for (auto& sm : raster.mesh->submeshes)
				{
					submesh_info submesh = sm.info;
					const ref<pbr_material>& material = sm.material;

					if (material->albedoTint.a < 1.f)
					{
						transparentRenderPass.renderObject(m, mesh.vertexBuffer, mesh.indexBuffer, submesh, material);
					}
					else
					{
						if (dynamic)
						{
							opaqueRenderPass.renderDynamicObject(m, lastM, mesh.vertexBuffer, mesh.indexBuffer, submesh, material, (uint32)entityHandle);
							projectorOpaqueRenderPass.renderDynamicObject(m, lastM, mesh.vertexBuffer, mesh.indexBuffer, submesh, material, (uint32)entityHandle);
						}
						else
						{
							if (true)
							{
								projectorManager->solver.simulateProjectors(&opaqueRenderPass, m, mesh.vertexBuffer, mesh.indexBuffer, submesh, (uint32)entityHandle);
							}
							else
							{
								opaqueRenderPass.renderStaticObject(m, mesh.vertexBuffer, mesh.indexBuffer, submesh, material, (uint32)entityHandle);
							}
							projectorOpaqueRenderPass.renderStaticObject(m, mesh.vertexBuffer, mesh.indexBuffer, submesh, material, (uint32)entityHandle);
						}
					}

					if (outline)
					{
						ldrRenderPass.renderOutline(m, mesh.vertexBuffer, mesh.indexBuffer, submesh);
					}
				}
			}
		}

		tracker->visualizeDepth(&ldrRenderPass);

		if (selectedEntity)
		{
			if (point_light_component* pl = selectedEntity.getComponentIfExists<point_light_component>())
			{
				position_component& pc = selectedEntity.getComponent<position_component>();

				renderWireSphere(pc.position, pl->radius, vec4(pl->color, 1.f), &ldrRenderPass);
			}
			else if (spot_light_component* sl = selectedEntity.getComponentIfExists<spot_light_component>())
			{
				position_rotation_component& prc = selectedEntity.getComponent<position_rotation_component>();

				renderWireCone(prc.position, prc.rotation * vec3(0.f, 0.f, -1.f),
					sl->distance, sl->outerAngle * 2.f, vec4(sl->color, 1.f), &ldrRenderPass);
			}
		}

		submitRenderPasses(numSpotShadowRenderPasses, numPointShadowRenderPasses);
	}
	else
	{
		if (dxContext.featureSupport.raytracing())
		{
			raytracingTLAS.reset();

			for (auto [entityHandle, transform, raytrace] : scene.group(entt::get<transform_component, raytrace_component>).each())
			{
				raytracingTLAS.instantiate(raytrace.type, transform);
			}

			raytracingTLAS.build();
		}
	}
#endif

	for (auto [entityHandle, transform, dynamic] : scene.group(entt::get<transform_component, dynamic_transform_component>).each())
	{
		dynamic = transform;
	}
}

void application::handleFileDrop(const fs::path& filename)
{
	fs::path path = filename;
	fs::path relative = fs::relative(path, fs::current_path());

	auto mesh = loadMeshFromFile(relative.string());
	if (mesh)
	{
		fs::path path = filename;
		path = path.stem();

		scene.createEntity(path.string().c_str())
			.addComponent<transform_component>(vec3(0.f), quat::identity)
			.addComponent<raster_component>(mesh);
	}
}
