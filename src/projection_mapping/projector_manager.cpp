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

			setupDirty |= ImGui::PropertyDisableableCheckbox("Server", isServerCheckbox, !protocol.initialized);
			if (!isServerCheckbox)
			{
				setupDirty |= ImGui::PropertyInputText("Server IP", protocol.serverIP, sizeof(protocol.serverIP));
				setupDirty |= ImGui::PropertyInput("Server port", protocol.serverPort);
			}

			if (ImGui::PropertyDisableableButton("Network", isServerCheckbox ? "Start" : "Connect", !protocol.initialized))
			{
				protocol.start(*scene, this, isServerCheckbox);
			}

			if (protocol.initialized && isServerCheckbox)
			{
				ImGui::PropertyInputText("Server IP", protocol.serverIP, sizeof(protocol.serverIP), true);
				ImGui::PropertyValue("Server port", protocol.serverPort);
			}

			ImGui::PropertySeparator();

			if (solver.settings.mode == projector_mode_demo)
			{
				ImGui::BeginDisabled();
			}
			ImGui::PropertyDropdown("Projector mode", projectorModeNames, 2, (uint32&)solver.settings.mode);
			if (solver.settings.mode == projector_mode_demo)
			{
				ImGui::EndDisabled();
			}
			ImGui::PropertyCheckbox("Apply solver intensity", solver.settings.applySolverIntensity);
			ImGui::PropertyCheckbox("Simulate all projectors", solver.settings.simulateAllProjectors);

			ImGui::PropertyCheckbox("Synthetic environment", simulationMode);

			if (simulationMode)
			{
				solver.settings.simulateAllProjectors = true;

				ImGui::PropertySlider("    Position calibration error", maxSimulatedPositionError, 0.f, 10.f, "%.2f mm");
				ImGui::PropertySliderAngle("    Rotation calibration error", maxSimulatedRotationError, 0.f, 1.f, "%.6f deg");
			}

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Depth discontinuity threshold", solver.settings.depthDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertySlider("Color discontinuity threshold", solver.settings.colorDiscontinuityThreshold, 0.f, 1.f);

			ImGui::PropertySlider("Depth hard distance", solver.settings.depthHardDistance, 0.f, 15.f);
			ImGui::PropertySlider("Depth smooth distance", solver.settings.depthSmoothDistance, 0.f, 15.f);
			ImGui::PropertySlider("Color hard distance", solver.settings.colorHardDistance, 0.f, 15.f);
			ImGui::PropertySlider("Color smooth distance", solver.settings.colorSmoothDistance, 0.f, 15.f);
			ImGui::PropertySlider("Best mask hard distance", solver.settings.bestMaskHardDistance, 0.f, 30.f);
			ImGui::PropertySlider("Best mask smooth distance", solver.settings.bestMaskSmoothDistance, 0.f, 50.f);

			ImGui::PropertySeparator();
			
			ImGui::PropertySlider("Color mask strength", solver.settings.colorMaskStrength, 0.f, 0.9f);
			
			ImGui::PropertySeparator();

			ImGui::PropertySlider("Reference distance", solver.settings.referenceDistance, 0.f, 5.f);
			ImGui::PropertySlider("Reference white", solver.settings.referenceWhite);

			ImGui::PropertySeparator();

			if (ImGui::PropertyDisableableButton("Demo", "Go", solver.settings.mode != projector_mode_demo))
			{
				solver.settings.mode = projector_mode_demo;
				solver.settings.demoPC = -1;
				solver.settings.demoMonitor = -1;

				demoTimer = 0.f; // Set to 0, such that it is evaluated right in this frame.
			}

			ImGui::PropertyCheckbox("Show projector frusta", showProjectorFrusta);

			ImGui::EndProperties();
		}

		if (setupDirty)
		{
			saveSetup();
		}


		if (isNetworkServer() && solver.settings.mode == projector_mode_demo)
		{
			demoTimer -= dt;
			if (demoTimer <= 0.f)
			{
				demoTimer += 1.f;

				auto& pc = solver.settings.demoPC;
				auto& mon = solver.settings.demoMonitor;

				while (true)
				{
					++mon;
					projector_check check = isProjector(pc, mon);
					
					if (check == projector_check_yes)
					{
						break;
					}

					if (check == projector_check_no)
					{
						continue;
					}

					assert(check == projector_check_out_of_range);
					++pc;
					mon = -1;

					if ((int32)pc > highestClientID)
					{
						solver.settings.mode = projector_mode_projection_mapping;
						break;
					}
				}
			}
		}

		projector_renderer::applySolverIntensity = solver.settings.applySolverIntensity;
		uint32 myClientID = protocol.client_getID();

		for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
		{
			bool black = (solver.settings.mode == projector_mode_calibration);

			if (solver.settings.mode == projector_mode_demo)
			{
				black = solver.settings.demoPC != myClientID || solver.settings.demoMonitor != i;
			}

			if (isProjectorIndex[i])
			{
				if (black != blackWindows[i].visible)
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
			if (solver.settings.mode == projector_mode_calibration)
			{
				tracker->disableTracking = false;
				tracker->mode = tracking_mode_track_camera;
			}
			else
			{
				tracker->disableTracking = true;

				if (tracker->camera.isInitialized() && tracker->camera.irProjectorOn)
				{
					tracker->camera.toggleIRProjector();
				}
			}
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
			if (ImGui::BeginTable("##Table", 8))
			{
				ImGui::TableSetupColumn("Projector");
				ImGui::TableSetupColumn("Rendering");
				ImGui::TableSetupColumn("Best mask");
				ImGui::TableSetupColumn("Discontinuities");
				ImGui::TableSetupColumn("Discontinuity distance field");
				ImGui::TableSetupColumn("Best mask distance field");
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
					hoverImage(projector.renderer.discontinuitiesTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.discontinuityDistanceFieldTexture);

					ImGui::TableNextColumn();
					hoverImage(projector.renderer.bestMaskDistanceFieldTexture);

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

	if (!isNetworkServer())
	{
		render_camera networkedViewerCamera;
		networkedViewerCamera.initializeIngame(networkCameraPosition, networkCameraRotation, deg2rad(70.f), 0.01f);
		networkedViewerCamera.setViewport(1000, 1000); // Doesn't matter. The shaders use position and rotation only.
		networkedViewerCamera.updateMatrices();

		projector_renderer::setViewerCamera(networkedViewerCamera);
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



	projectorIndex = numProjectors - 1; // EnTT iterates back to front.
	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
	{
		if (!projector.headless || solver.settings.simulateAllProjectors || simulationMode)
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


			if (simulationMode)
			{
				scene_entity entity = { entityHandle, *scene };
				position_rotation_component& transform = entity.getComponent<position_rotation_component>();

				vec3 position = transform.position;
				quat rotation = transform.rotation;
				camera_intrinsics intrinsics = projector.intrinsics;

				random_number_generator rng = { 619324 * (uint32)entityHandle }; // Random, but deterministic.

				position += rng.randomPointOnUnitSphere() * (maxSimulatedPositionError * 0.001f);
				rotation = rng.randomRotation(maxSimulatedRotationError) * rotation;

				render_camera& camera = projectorCameras[projectorIndex--];
				camera.initializeCalibrated(position, rotation, projector.width, projector.height, intrinsics, 0.01f);
				camera.updateMatrices();

				projector.renderer.setProjectorCamera(camera);
				projector.renderer.rerenderDepthBuffer(cl);
			}


			dxContext.executeCommandList(cl);

			if (shouldPresent)
			{
				projector.window.swapBuffers();
			}
		}
	}

	if (simulationMode)
	{
		solver.resetCameras(scene->raw<projector_component>(), projectorCameras, numProjectors);
	}
}

void projector_manager::onSceneLoad()
{
	protocol.server_broadcastObjectInfo();

	createProjectorsAndNotify();
}

void projector_manager::reportLocalCalibration(const std::unordered_map<std::string, projector_calibration>& calib)
{
	for (const auto& c : calib)
	{
		context.knownProjectorCalibrations[c.first] = c.second;
	}

	createProjectorsAndNotify();
}

bool projector_manager::isNetworkServer()
{
	return !protocol.initialized || protocol.isServer;
}

projector_manager::projector_check projector_manager::isProjector(uint32 pc, uint32 monitor)
{
	if (pc == -1)
	{
		if (monitor >= (uint32)win32_window::allConnectedMonitors.size())
		{
			return projector_check_out_of_range;
		}
		return isProjectorIndex[monitor] ? projector_check_yes : projector_check_no;
	}
	else
	{
		auto it = clients.find(pc);
		if (it != clients.end())
		{
			auto& client = it->second;
			if (monitor >= client.monitors.size())
			{
				return projector_check_out_of_range;
			}

			return (context.knownProjectorCalibrations.find(client.monitors[monitor].uniqueID) != context.knownProjectorCalibrations.end()) ? projector_check_yes : projector_check_no;
		}
		return projector_check_out_of_range;
	}
}

void projector_manager::createProjectorsAndNotify()
{
	if (!protocol.initialized)
	{
		std::vector<projector_instantiation> instantiations = createInstantiations();
		createProjectors(instantiations);
	}
	else
	{
		if (protocol.isServer)
		{
			std::vector<projector_instantiation> instantiations = createInstantiations();

			createProjectors(instantiations);
			protocol.server_broadcastProjectors(instantiations);
		}
		else
		{
			protocol.client_reportLocalCalibration(context.knownProjectorCalibrations);
		}
	}
}

std::vector<projector_instantiation> projector_manager::createInstantiations()
{
	std::vector<projector_instantiation> instantiations;

	if (!simulationMode)
	{
		auto& monitors = win32_window::allConnectedMonitors;
		for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
		{
			if (isProjectorIndex[i])
			{
				auto& monitor = monitors[i];

				auto it = context.knownProjectorCalibrations.find(monitor.uniqueID);
				if (it != context.knownProjectorCalibrations.end())
				{
					instantiations.push_back(projector_instantiation{ (uint32)-1, i, it->second });
				}
			}
		}

		for (auto& client : clients)
		{
			auto& monitors = client.second.monitors;
			for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
			{
				auto& monitor = monitors[i];

				auto it = context.knownProjectorCalibrations.find(monitor.uniqueID);
				if (it != context.knownProjectorCalibrations.end())
				{
					instantiations.push_back(projector_instantiation{ client.second.clientID, i, it->second });
				}
			}
		}
	}
	else
	{
		uint32 i = 0;
		for (auto& calib : context.knownProjectorCalibrations)
		{
			instantiations.push_back(projector_instantiation{ 9999, i++, calib.second });
		}
	}

	return instantiations;
}

void projector_manager::network_newClient(const std::string& hostname, uint32 clientID, const std::vector<std::string>& descriptions, const std::vector<std::string>& uniqueIDs)
{
	client_info info;

	info.clientID = clientID;
	info.hostname = hostname;

	for (uint32 i = 0; i < (uint32)uniqueIDs.size(); ++i)
	{
		info.monitors.push_back({ descriptions[i], uniqueIDs[i] });
	}
	
	clients[clientID] = info;

	highestClientID = max((int32)clientID, highestClientID);

	createProjectorsAndNotify();
}

void projector_manager::network_clientCalibration(uint32 clientID, const std::vector<client_calibration_message>& calibrations)
{
	auto it = clients.find(clientID);
	assert(it != clients.end());

	client_info& info = it->second;

	for (const client_calibration_message& msg : calibrations)
	{
		uint32 monitorIndex = msg.monitorIndex;
		const std::string& uniqueID = info.monitors[monitorIndex].uniqueID;

		context.knownProjectorCalibrations[uniqueID] = msg.calibration;
	}

	createProjectorsAndNotify();
}

void projector_manager::network_projectorInstantiations(const std::vector<projector_instantiation>& instantiations)
{
	createProjectors(instantiations);
}

void projector_manager::loadSetup()
{
	std::ifstream stream("setup");
	YAML::Node n = YAML::Load(stream);

	for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
	{
		if (auto monitorNode = n[win32_window::allConnectedMonitors[i].uniqueID]) {	isProjectorIndex[i] = monitorNode.as<bool>(); }
	}

	if (auto serverNode = n["Server"]) { isServerCheckbox = serverNode.as<bool>(); }

	if (!isServerCheckbox)
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

	out << YAML::Key << "Server" << YAML::Value << isServerCheckbox;
	if (!isServerCheckbox)
	{
		out << YAML::Key << "Server IP" << YAML::Value << protocol.serverIP;
		out << YAML::Key << "Server Port" << YAML::Value << protocol.serverPort;
	}

	out << YAML::EndMap;

	std::ofstream fout("setup");
	fout << out.c_str();
}

void projector_manager::createProjectors(const std::vector<projector_instantiation>& instantiations)
{
	// Delete all current projectors.
	auto projectorGroup = scene->view<projector_component>();

	if (projectorGroup.size() != 0)
	{
		LOG_MESSAGE("Deleting current projectors");
	}

	scene->registry.destroy(projectorGroup.begin(), projectorGroup.end());

	uint32 myClientID = protocol.client_getID();

	for (auto& inst : instantiations)
	{
		if (inst.clientID == myClientID)
		{
			createProjector(win32_window::allConnectedMonitors[inst.monitorIndex].uniqueID, true, inst.calibration);
		}
		else
		{
			createProjector("Remote", false, inst.calibration);
		}
	}
}

void projector_manager::createProjector(const std::string& monitorID, bool local, const projector_calibration& calib)
{
	const char* name = local ? "Projector (local)" : "Projector (remote)";

	scene->createEntity(name)
		.addComponent<position_rotation_component>(calib.position, calib.rotation)
		.addComponent<projector_component>(calib.width, calib.height, calib.intrinsics, monitorID, local, true);
}


