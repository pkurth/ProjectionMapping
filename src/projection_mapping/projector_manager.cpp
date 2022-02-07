#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"
#include "rendering/debug_visualization.h"

#include "post_processing_rs.hlsli"

#include "projector_network_protocol.h"

projector_manager::projector_manager(game_scene& scene)
{
	this->scene = &scene;
	solver.initialize();
}

void projector_manager::beginFrame()
{
	for (auto& [entityHandle, projector] : scene->view<projector_component>().each())
	{
		projector.renderer.beginFrame();
	}
}

void projector_manager::updateAndRender(float dt)
{
	if (ImGui::Begin("Projection Mapping"))
	{
		if (ImGui::BeginProperties())
		{
			ImGui::PropertyDisableableCheckbox("Server", isServer, !projectorNetworkInitialized);
			if (!isServer)
			{
				ImGui::PropertyInputText("Server IP", SERVER_IP, sizeof(SERVER_IP));
				ImGui::PropertyInput("Server port", SERVER_PORT);
			}

			if (ImGui::PropertyDisableableButton("Network", isServer ? "Start" : "Connect", !projectorNetworkInitialized))
			{
				startProjectorNetworkProtocol(*scene, this, isServer);
			}

			if (projectorNetworkInitialized && isServer)
			{
				ImGui::PropertyInputText("Server IP", SERVER_IP, sizeof(SERVER_IP), true);
				ImGui::PropertyValue("Server port", SERVER_PORT);
			}

			ImGui::PropertySeparator();

			ImGui::PropertyCheckbox("Apply solver intensity", solver.settings.applySolverIntensity);

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Depth discontinuity threshold", solver.settings.depthDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertyDrag("Depth discontinuity dilate radius", solver.settings.depthDiscontinuityDilateRadius);
			ImGui::PropertyDrag("Depth discontinuity smooth radius", solver.settings.depthDiscontinuitySmoothRadius);

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Color discontinuity threshold", solver.settings.colorDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertyDrag("Color discontinuity dilate radius", solver.settings.colorDiscontinuityDilateRadius);
			ImGui::PropertyDrag("Color discontinuity smooth radius", solver.settings.colorDiscontinuitySmoothRadius);

			ImGui::PropertySeparator();
			
			ImGui::PropertySlider("Color mask strength", solver.settings.colorMaskStrength);
			
			ImGui::PropertySeparator();

			ImGui::PropertySlider("Reference distance", solver.settings.referenceDistance, 0.f, 5.f);
			ImGui::PropertySlider("Reference white", solver.settings.referenceWhite);

			ImGui::EndProperties();
		}

		projector_renderer::applySolverIntensity = solver.settings.applySolverIntensity;

		if (ImGui::Button("Detailed view"))
		{
			detailWindowOpen = true;
		}

		if (ImGui::BeginTree("Context"))
		{
			if (ImGui::BeginProperties())
			{
				ImGui::PropertyValue("Known projector calibrations", "");
				ImGui::PropertySeparator();
				for (auto& c : context.knownProjectorCalibrations)
				{
					ImGui::PropertyValue("Monitor", c.first.c_str());

					const auto& p = c.second;
					ImGui::PropertyValue("Position", p.position);
					ImGui::PropertyValue("Rotation", p.rotation);
					ImGui::PropertyValue("Width", p.width);
					ImGui::PropertyValue("Height", p.height);
					ImGui::PropertyValue("Intrinsics", *(vec4*)&p.intrinsics, "%.1f, %.1f, %.1f, %.1f");

					ImGui::PropertySeparator();
				}

				ImGui::EndProperties();
			}

			ImGui::EndTree();
		}
	}
	ImGui::End();

	updateProjectorNetworkProtocol(dt);


	if (detailWindowOpen)
	{
		if (ImGui::Begin("Projector details", &detailWindowOpen, ImGuiWindowFlags_NoDocking))
		{
			if (ImGui::BeginTable("##Table", 7))
			{
				ImGui::TableSetupColumn("Projector");
				ImGui::TableSetupColumn("Rendering");
				ImGui::TableSetupColumn("Best mask");
				ImGui::TableSetupColumn("Depth discontinuities");
				ImGui::TableSetupColumn("Color discontinuities");
				ImGui::TableSetupColumn("Masks");
				ImGui::TableSetupColumn("Solver intensities");

				ImGui::TableHeadersRow();

				auto hoverImage = [](const ref<dx_texture>& tex)
				{
					ImGui::PushID(&tex);
					ImGui::Image(tex);
					if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
					{
						ImVec2 mousePos = ImGui::GetIO().MousePos;
						ImGui::GetIO().MousePos.x -= tex->width / 2;

						ImGui::BeginTooltip();
						ImGui::Image(tex, tex->width, tex->height);
						ImGui::EndTooltip();

						ImGui::GetIO().MousePos = mousePos;
					}
					ImGui::PopID();
				};

				uint32 i = 0;
				for (auto& [entityHandle, projector] : scene->view<projector_component>().each())
				{
					ImGui::TableNextRow();
					
					ImGui::TableNextColumn();
					ImGui::Text("%u (%s)", i, (projector.headless ? "remote" : "local"));

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.frameResult);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.bestMaskTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.depthDiscontinuitiesTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.colorDiscontinuitiesTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.maskTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.solverIntensityTexture);

					++i;
				}

				ImGui::EndTable();
			}
		}
		ImGui::End();
	}


	uint32 numProjectors = scene->numberOfComponentsOfType<projector_component>();
	render_camera projectorCameras[32];

	uint32 projectorIndex = numProjectors - 1; // EnTT iterates back to front.
	// We use a view here, because we need the projectors sorted the same way as the raw array. An alternative would be to group projectors with position_rotation_components,
	// but this conflicts with the owning group of spot lights and position_rotations.
	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
	{
		scene_entity entity = { entityHandle, *scene };
		position_rotation_component& transform = entity.getComponent<position_rotation_component>();

		render_camera& camera = projectorCameras[projectorIndex--];
		camera.initializeCalibrated(transform.position, transform.rotation, projector.width, projector.height, projector.intrinsics, 0.01f);
		camera.updateMatrices();

		projector.renderer.setProjectorCamera(camera);

		projector.renderer.endFrame();
	}


	solver.solve(scene->raw<projector_component>(), projectorCameras, numProjectors);




	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
	{
		dx_command_list* cl = dxContext.getFreeRenderCommandList();

		bool shouldPresent = projector.shouldPresent();
		projector.renderer.finalizeImage(cl);

		if (shouldPresent)
		{
			dx_resource backbuffer = projector.window.backBuffers[projector.window.currentBackbufferIndex];
			cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			cl->copyResource(projector.renderer.frameResult->resource, backbuffer);
			cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		}

		dxContext.executeCommandList(cl);

		if (shouldPresent)
		{
			projector.window.swapBuffers();
		}
	}
}

void projector_manager::onSceneLoad()
{
	std::vector<std::string> myProjectors = getLocalProjectors();
	std::vector<std::string> remoteProjectors = getRemoteProjectors();

	createProjectors(myProjectors, remoteProjectors);
	notifyClients(myProjectors, remoteProjectors);
}

void projector_manager::notifyClients(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors)
{
	std::vector<std::string> allProjectors;
	allProjectors.insert(allProjectors.end(), myProjectors.begin(), myProjectors.end());
	allProjectors.insert(allProjectors.end(), remoteProjectors.begin(), remoteProjectors.end());

	notifyProjectorNetworkOnSceneLoad(context, allProjectors);
}

void projector_manager::onMessageFromClient(const std::vector<std::string>& remoteMonitors)
{
	this->remoteMonitors.insert(remoteMonitors.begin(), remoteMonitors.end());


	std::vector<std::string> myProjectors = getLocalProjectors();
	std::vector<std::string> remoteProjectors = getRemoteProjectors();

	createProjectors(myProjectors, remoteProjectors);
	notifyClients(myProjectors, remoteProjectors);
}

void projector_manager::onMessageFromServer(std::unordered_map<std::string, projector_calibration>&& calibrations, const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors)
{
	this->context.knownProjectorCalibrations = std::move(calibrations);

	createProjectors(myProjectors, remoteProjectors);
}

void projector_manager::reportLocalCalibration(const std::string& monitor, camera_intrinsics intrinsics, uint32 width, uint32 height, vec3 position, quat rotation)
{
	context.knownProjectorCalibrations[monitor] = { rotation, position, width, height, intrinsics };

	std::vector<std::string> myProjectors = getLocalProjectors();
	std::vector<std::string> remoteProjectors = getRemoteProjectors();

	createProjectors(myProjectors, remoteProjectors);
	notifyClients(myProjectors, remoteProjectors);
}

void projector_manager::createProjectors(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors)
{
	LOG_MESSAGE("Deleting current projectors");

	// Delete all current projectors.
	auto projectorGroup = scene->view<projector_component>();
	scene->registry.destroy(projectorGroup.begin(), projectorGroup.end());

	for (auto& monitorID : myProjectors)
	{
		createProjector(monitorID, true);
	}

	for (auto& monitorID : remoteProjectors)
	{
		createProjector(monitorID, false);
	}
}

void projector_manager::createProjector(const std::string& monitorID, bool local)
{
	auto it = context.knownProjectorCalibrations.find(monitorID);
	assert(it != context.knownProjectorCalibrations.end());

	auto p = it->second;

	vec3 position = p.position;
	quat rotation = p.rotation;

	//pos = setupRotation * pos;
	//rotation = setupRotation * rotation;

	const char* name = local ? "Projector (local)" : "Projector (remote)";

	scene->createEntity(name)
		.addComponent<position_rotation_component>(position, rotation)
		.addComponent<projector_component>(p.width, p.height, p.intrinsics, monitorID, local, true);
}

std::vector<std::string> projector_manager::getLocalProjectors()
{
	std::vector<std::string> myProjectors;

	for (const monitor_info& monitor : win32_window::allConnectedMonitors)
	{
		auto it = context.knownProjectorCalibrations.find(monitor.uniqueID);
		if (it != context.knownProjectorCalibrations.end())
		{
			myProjectors.push_back(monitor.uniqueID);
		}
	}

	return myProjectors;
}

std::vector<std::string> projector_manager::getRemoteProjectors()
{
	std::vector<std::string> remoteProjectors;

	for (const std::string& monitorID : remoteMonitors)
	{
		auto it = context.knownProjectorCalibrations.find(monitorID);
		if (it != context.knownProjectorCalibrations.end())
		{
			remoteProjectors.push_back(monitorID);
		}
	}

	return remoteProjectors;
}

