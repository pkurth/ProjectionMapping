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
	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
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
			ImGui::PropertySlider("Depth discontinuity threshold", projector_renderer::depthDiscontinuityThreshold, 0.f, 1.f);
			ImGui::PropertySlider("Depth discontinuity dilate radius", projector_renderer::depthDiscontinuityDilateRadius, 0, MORPHOLOGY_MAX_RADIUS);
			ImGui::PropertyCheckbox("Blur depth discontinuities", projector_renderer::blurDepthDiscontinuities);

			ImGui::PropertySlider("Reference distance", solver.referenceDistance, 0.f, 5.f);

			ImGui::EndProperties();
		}



		for (auto [entityHandle, projector] : scene->view<projector_component>().each())
		{
			//ImGui::Image(projector.renderer.solverIntensity, 400, (uint32)(400 / projector.camera.aspect));
			ImGui::Image(projector.renderer.depthDiscontinuitiesTexture, 400, (uint32)(400 / projector.camera.aspect));
		}
	}
	ImGui::End();



	for (auto [entityHandle, projector, transform] : scene->group(entt::get<projector_component, position_rotation_component>).each())
	{
		if (projector.renderer.active)
		{
			projector.camera.position = transform.position;
			projector.camera.rotation = transform.rotation;
			projector.camera.setViewport(projector.window.clientWidth, projector.window.clientHeight);
			projector.camera.updateMatrices();

			projector.renderer.setProjectorCamera(projector.camera);
			projector.renderer.setViewerCamera(scene->camera);
			projector.renderer.setSun(scene->sun);
			projector.renderer.setEnvironment(scene->environment);

			projector.renderer.endFrame();
		}
	}


	solver.prepareForFrame(scene->raw<projector_component>(), scene->numberOfComponentsOfType<projector_component>());
	solver.solve();




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

