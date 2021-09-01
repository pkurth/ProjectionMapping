#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"

#include "projector_solver.h"

projector_manager::projector_manager()
{
	monitors = getAllDisplayDevices();

	for (auto& monitor : monitors)
	{
		physicalProjectors.emplace_back(monitor);
	}

	addDummyProjector();
	addDummyProjector();

	initializeProjectorSolver();
}

void projector_manager::beginFrame()
{
	opaqueRenderPass = 0;
	environment = 0;
}

void projector_manager::updateAndRender()
{
	if (ImGui::Begin("Projectors"))
	{
		if (ImGui::TreeNode("Physical projectors"))
		{
			for (auto& p : physicalProjectors)
			{
				p.edit();
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Dummy projectors"))
		{
			for (auto& p : dummyProjectors)
			{
				p.edit();
			}

			if (ImGui::Button("Add"))
			{
				addDummyProjector();
			}

			ImGui::TreePop();
		}
	}

	ImGui::End();

	std::vector<projector_solver_input> solverInput;

	for (auto& p : physicalProjectors)
	{
		if (p.active())
		{
			p.render(opaqueRenderPass, sun, environment, viewerCamera);
			solverInput.push_back({ p.renderer.frameResult, p.renderer.depthStencilBuffer, p.renderer.solverIntensity, p.camera.viewProj });
		}
	}
	for (auto& p : dummyProjectors)
	{
		if (p.active())
		{
			p.render(opaqueRenderPass, sun, environment, viewerCamera);
			solverInput.push_back({ p.renderer.frameResult, p.renderer.depthStencilBuffer, p.renderer.solverIntensity, p.camera.viewProj });
		}
	}

	solveProjectorIntensities(solverInput, 1);
}

void projector_manager::debugDraw()
{
	for (auto& p : dummyProjectors)
	{
		ImGui::Image(p.renderer.solverIntensity, 400, 300);
	}
}

void projector_manager::addDummyProjector()
{
	static const vec3 possiblePositions[] =
	{
		vec3(0.5f, 1.f, 2.1f),
		vec3(-0.5f, 1.f, 2.1f),
	};

	static const quat possibleRotations[] =
	{
		quat(vec3(0.f, 1.f, 0.f), deg2rad(20.f)),
		quat(vec3(0.f, 1.f, 0.f), deg2rad(-20.f)),
	};

	uint32 index = (uint32)dummyProjectors.size();
	if (index < arraysize(possiblePositions))
	{
		dummyProjectors.emplace_back("Dummy " + std::to_string(dummyProjectors.size()), possiblePositions[index], possibleRotations[index], 640, 480, camera_intrinsics{ 400.f, 400.f, 320.f, 240.f });
	}
}

