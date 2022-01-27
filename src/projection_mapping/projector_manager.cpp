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
	if (ImGui::Begin("Projectors"))
	{
		if (ImGui::BeginProperties())
		{
			ImGui::PropertyDisableableCheckbox("Server", isServer, !projectorNetworkInitialized);
			if (!isServer)
			{
				ImGui::PropertyInputText("Server IP", SERVER_IP, sizeof(SERVER_IP));
				ImGui::PropertyInput("Server port", SERVER_PORT);
			}

			if (ImGui::PropertyDisableableButton("Network", "Start", !projectorNetworkInitialized))
			{
				startProjectorNetworkProtocol(*scene, solver, isServer);
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

			ImGui::EndProperties();
		}

		projector_renderer::applySolverIntensity = solver.settings.applySolverIntensity;

		if (ImGui::Button("Detailed view"))
		{
			detailWindowOpen = true;
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
					ImGui::Text("%u", i);

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

	for (auto [entityHandle, projector, transform] : scene->group(entt::get<projector_component, position_rotation_component>).each())
	{
		projector.calibratedCamera.position = transform.position;
		projector.calibratedCamera.rotation = transform.rotation;
		projector.calibratedCamera.updateMatrices();

		projector.renderer.setProjectorCamera(projector.calibratedCamera);

		projector.renderer.endFrame();
	}


	solver.solve(scene->raw<projector_component>(), scene->numberOfComponentsOfType<projector_component>());




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
	notifyProjectorNetworkOnSceneLoad();
}

