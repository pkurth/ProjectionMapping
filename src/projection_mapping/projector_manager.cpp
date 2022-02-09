#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"
#include "rendering/debug_visualization.h"
#include "core/yaml.h"
#include "tracking/tracking.h"

#include "post_processing_rs.hlsli"


projector_manager::projector_manager(game_scene& scene, depth_tracker* tracker)
{
	this->scene = &scene;
	solver.initialize();

	loadSetup();

	static uint8 black = 0;

	for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
	{
		auto& monitor = win32_window::allConnectedMonitors[i];

		blackWindows[i].initialize(L"Black", 1, 1, &black, 1, 1, 1);
		blackWindows[i].moveToMonitor(monitor);
		blackWindows[i].toggleFullscreen();
		blackWindows[i].setAlwaysOnTop();
		blackWindows[i].toggleVisibility(); // Make invisible.
	}

	this->tracker = tracker;
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
	bool setupDirty = false;

	if (ImGui::Begin("Projection Mapping"))
	{
		auto& monitors = win32_window::allConnectedMonitors;

		if (ImGui::BeginTree("Local projectors"))
		{
			if (ImGui::BeginTable("##ProjTable", 2, ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Monitor");
				ImGui::TableSetupColumn("Is projector");

				ImGui::TableHeadersRow();

				for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
				{
					ImGui::PushID(i);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text(monitors[i].description.c_str());

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text(monitors[i].uniqueID.c_str());
						ImGui::EndTooltip();
					}

					ImGui::TableNextColumn();
					setupDirty |= ImGui::Checkbox("##isProj", &isProjectorIndex[i]);

					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::EndTree();
		}
		

		if (ImGui::BeginProperties())
		{
			ImGui::PropertySeparator();
			ImGui::PropertySeparator();

			setupDirty |= ImGui::PropertyDisableableCheckbox("Server", isServer, !protocol.initialized);
			if (!isServer)
			{
				setupDirty |= ImGui::PropertyInputText("Server IP", protocol.serverIP, sizeof(protocol.serverIP));
				setupDirty |= ImGui::PropertyInput("Server port", protocol.serverPort);
			}

			if (ImGui::PropertyDisableableButton("Network", isServer ? "Start" : "Connect", !protocol.initialized))
			{
				protocol.start(*scene, this, isServer);
			}

			if (protocol.initialized && isServer)
			{
				ImGui::PropertyInputText("Server IP", protocol.serverIP, sizeof(protocol.serverIP), true);
				ImGui::PropertyValue("Server port", protocol.serverPort);
			}

			ImGui::PropertySeparator();

			ImGui::PropertyDropdown("Projector mode", projectorModeNames, arraysize(projectorModeNames), (uint32&)solver.settings.mode);
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

		if (setupDirty)
		{
			saveSetup();
		}

		bool calibrationMode = (solver.settings.mode == projector_mode_calibration);

		projector_renderer::applySolverIntensity = solver.settings.applySolverIntensity;
		
		for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
		{
			if (isProjectorIndex[i])
			{
				if (calibrationMode != blackWindows[i].visible)
				{
					blackWindows[i].toggleVisibility();
				}
			}
			else
			{
				if (blackWindows[i].visible)
				{
					blackWindows[i].toggleVisibility();
				}
			}
		}

		if (protocol.initialized && !protocol.isServer)
		{
			tracker->disableTracking = !calibrationMode;
		}

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

	protocol.update(dt);


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

	protocol.ifServer_broadcastObjectInfo();
}

void projector_manager::reportLocalCalibration(const std::string& uniqueID, const projector_calibration& calib)
{
	context.knownProjectorCalibrations[uniqueID] = calib;

	std::vector<std::string> myProjectors = getLocalProjectors();
	std::vector<std::string> remoteProjectors = getRemoteProjectors();

	createProjectors(myProjectors, remoteProjectors);
	//protocol.reportCalibration
}

void projector_manager::network_newClient(const std::string& hostname, uint32 clientID, const std::vector<std::string>& descriptions, const std::vector<std::string>& uniqueIDs)
{
	client_info info;

	info.clientID = clientID;

	for (uint32 i = 0; i < (uint32)uniqueIDs.size(); ++i)
	{
		info.monitors.push_back({ descriptions[i], uniqueIDs[i] });
	}
	
	clients[hostname] = info;
}

void projector_manager::loadSetup()
{
	std::ifstream stream("setup");
	YAML::Node n = YAML::Load(stream);

	for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
	{
		if (auto monitorNode = n[win32_window::allConnectedMonitors[i].uniqueID]) {	isProjectorIndex[i] = monitorNode.as<bool>(); }
	}

	if (auto serverNode = n["Server"]) { isServer = serverNode.as<bool>(); }

	if (!isServer)
	{
		if (auto sNode = n["Server IP"]) { strncpy(protocol.serverIP, sNode.as<std::string>().c_str(), sizeof(protocol.serverIP)); }
		if (auto sNode = n["Server Port"]) { protocol.serverPort = sNode.as<uint32>(); }
	}
}

void projector_manager::saveSetup()
{
	YAML::Emitter out;

	out << YAML::BeginMap;
	for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
	{
		out << YAML::Key << win32_window::allConnectedMonitors[i].uniqueID << YAML::Value << isProjectorIndex[i];
	}

	out << YAML::Key << "Server" << YAML::Value << isServer;
	if (!isServer)
	{
		out << YAML::Key << "Server IP" << YAML::Value << protocol.serverIP;
		out << YAML::Key << "Server Port" << YAML::Value << protocol.serverPort;
	}

	out << YAML::EndMap;

	std::ofstream fout("setup");
	fout << out.c_str();
}

void projector_manager::createProjectors(const std::vector<std::string>& myProjectors, const std::vector<std::string>& remoteProjectors)
{
	// Delete all current projectors.
	auto projectorGroup = scene->view<projector_component>();

	if (projectorGroup.size() != 0)
	{
		LOG_MESSAGE("Deleting current projectors");
	}

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

	const char* name = local ? "Projector (local)" : "Projector (remote)";

	scene->createEntity(name)
		.addComponent<position_rotation_component>(p.position, p.rotation)
		.addComponent<projector_component>(p.width, p.height, p.intrinsics, monitorID, local, true);
}

std::vector<std::string> projector_manager::getLocalProjectors()
{
	std::vector<std::string> myProjectors;

	uint32 index = 0;
	for (const monitor_info& monitor : win32_window::allConnectedMonitors)
	{
		if (isProjectorIndex[index++])
		{
			auto it = context.knownProjectorCalibrations.find(monitor.uniqueID);
			if (it != context.knownProjectorCalibrations.end())
			{
				myProjectors.push_back(monitor.uniqueID);
			}
		}
	}

	return myProjectors;
}

std::vector<std::string> projector_manager::getRemoteProjectors()
{
	std::vector<std::string> remoteProjectors;

	for (auto& client : clients)
	{
		for (const auto& monitor : client.second.monitors)
		{
			auto it = context.knownProjectorCalibrations.find(monitor.uniqueID);
			if (it != context.knownProjectorCalibrations.end())
			{
				remoteProjectors.push_back(monitor.uniqueID);
			}
		}
	}

	return remoteProjectors;
}

