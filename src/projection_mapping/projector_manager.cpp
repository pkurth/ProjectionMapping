#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"
#include "rendering/debug_visualization.h"

#include "post_processing_rs.hlsli"


projector_manager::projector_manager(game_scene& scene)
{
	this->scene = &scene;
	solver.initialize();
}

void projector_manager::beginFrame()
{
	for (auto& [entityHandle, projector] : scene->view<projector_component>().each())
	{
		projector.renderer.beginFrame(projector.window.clientWidth, projector.window.clientHeight);
	}
}

void projector_manager::updateAndRender()
{
	if (ImGui::Begin("Projectors"))
	{
		if (ImGui::BeginProperties())
		{
			ImGui::PropertyCheckbox("Apply solver intensity", projector_renderer::applySolverIntensity);

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Depth discontinuity threshold", projector_renderer::depthDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertyDrag("Depth discontinuity dilate radius", projector_renderer::depthDiscontinuityDilateRadius);
			ImGui::PropertyDrag("Depth discontinuity smooth radius", projector_renderer::depthDiscontinuitySmoothRadius);

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Color discontinuity threshold", projector_renderer::colorDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertyDrag("Color discontinuity dilate radius", projector_renderer::colorDiscontinuityDilateRadius);
			ImGui::PropertyDrag("Color discontinuity smooth radius", projector_renderer::colorDiscontinuitySmoothRadius);

			ImGui::PropertySeparator();

			ImGui::PropertySlider("Reference distance", solver.referenceDistance, 0.f, 5.f);
			ImGui::PropertySlider("Regularization strength", solver.regularizationStrength);

			ImGui::PropertyCheckbox("Simulate calibration error", solver.simulateCalibrationError);

			ImGui::EndProperties();
		}

		if (ImGui::Button("Detailed view"))
		{
			detailWindowOpen = true;
		}
	}
	ImGui::End();


	if (detailWindowOpen)
	{
		if (ImGui::Begin("Projector details", &detailWindowOpen, ImGuiWindowFlags_NoDocking))
		{
			if (ImGui::BeginTable("##Table", 6))
			{
				ImGui::TableSetupColumn("Projector");
				ImGui::TableSetupColumn("Rendering");
				ImGui::TableSetupColumn("Best mask");
				ImGui::TableSetupColumn("Depth discontinuities");
				ImGui::TableSetupColumn("Color discontinuities");
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
		if (projector.renderer.active)
		{
			projector.calibratedCamera.position = transform.position;
			projector.calibratedCamera.rotation = transform.rotation;
			projector.calibratedCamera.setViewport(projector.window.clientWidth, projector.window.clientHeight);
			projector.calibratedCamera.updateMatrices();


			projector.realCamera = projector.calibratedCamera;

			if (solver.simulateCalibrationError)
			{
				assert(projector.calibratedCamera.type == camera_type_ingame); // For now. If we have a calibrated camera, we need to jitter different stuff.

				random_number_generator rng = { (uint32)entityHandle * 519251 }; // Random, but deterministic.
				const float maxPositionError = 0.01f;
				const float maxRotationError = deg2rad(0.5f);
				const float maxFovError = deg2rad(0.5f);

				projector.realCamera.position += rng.randomVec3Between(-maxPositionError, maxPositionError);
				projector.realCamera.rotation = rng.randomRotation(maxRotationError) * projector.realCamera.rotation;
				projector.realCamera.verticalFOV += rng.randomFloatBetween(-maxFovError, maxFovError);
				projector.realCamera.updateMatrices();
			}


			projector.renderer.setProjectorCamera(projector.calibratedCamera);
			projector.renderer.setRealProjectorCamera(projector.realCamera);

			projector.renderer.endFrame();
		}
	}


	solver.solve(scene->raw<projector_component>(), scene->numberOfComponentsOfType<projector_component>());




	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
	{
		if (projector.renderer.active)
		{
			dx_command_list* cl = dxContext.getFreeRenderCommandList();

			projector.renderer.finalizeImage(cl);

			dx_resource backbuffer = projector.window.backBuffers[projector.window.currentBackbufferIndex];
			cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			cl->copyResource(projector.renderer.frameResult->resource, backbuffer);
			cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

			dxContext.executeCommandList(cl);

			projector.window.swapBuffers();
		}
	}
}

