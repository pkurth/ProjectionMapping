#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"
#include "rendering/debug_visualization.h"


projector_manager::projector_manager(game_scene& scene)
{
	this->scene = &scene;
	initializeProjectorSolver();
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
		ImGui::Checkbox("Apply solver intensity", &applySolverIntensity);


		std::vector<projector_solver_input> solverInput;

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

				projector_solver_input si =
				{
					projector.renderer.frameResult,
					projector.renderer.worldNormalsTexture,
					projector.renderer.depthStencilBuffer,
					projector.renderer.solverIntensity,
					projector.camera.viewProj,
					projector.camera.position
				};

				solverInput.push_back(si);

				ImGui::Image(projector.renderer.solverIntensity, 400, (uint32)(400 / projector.camera.aspect));
			}
		}

		solveProjectorIntensities(solverInput, 1);



		for (auto [entityHandle, projector] : scene->view<projector_component>().each())
		{
			if (projector.renderer.active)
			{
				dx_command_list* cl = dxContext.getFreeRenderCommandList();

				projector.renderer.finalizeImage(cl, applySolverIntensity);

				dx_resource backbuffer = projector.window.backBuffers[projector.window.currentBackbufferIndex];
				cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
				cl->copyResource(projector.renderer.frameResult->resource, backbuffer);
				cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

				dxContext.executeCommandList(cl);

				projector.window.swapBuffers();
			}
		}

	}
	ImGui::End();
}

