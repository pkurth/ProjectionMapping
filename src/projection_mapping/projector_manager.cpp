#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"
#include "rendering/debug_visualization.h"


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
		ImGui::Checkbox("Apply solver intensity", &applySolverIntensity);

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

	for (auto& p : physicalProjectors)	{ if (p.active()) { ImGui::Image(p.renderer.solverIntensity, 400, 300); } }
	for (auto& p : dummyProjectors)		{ if (p.active()) { ImGui::Image(p.renderer.solverIntensity, 400, 300); } }

	ImGui::End();






	std::vector<projector_solver_input> solverInput;

	for (auto& p : physicalProjectors)
	{
		p.render(opaqueRenderPass, sun, environment, viewerCamera);
		if (p.active())
		{
			solverInput.push_back(p.getSolverInput());
		}
	}
	for (auto& p : dummyProjectors)
	{
		p.render(opaqueRenderPass, sun, environment, viewerCamera);
		if (p.active())
		{
			solverInput.push_back(p.getSolverInput());
		}
	}


	solveProjectorIntensities(solverInput, 1);


	// Present and swap buffers.
	for (auto& p : physicalProjectors)
	{
		p.presentToBackBuffer(applySolverIntensity);
		p.swapBuffers();
	}
	for (auto& p : dummyProjectors)
	{
		p.presentToBackBuffer(applySolverIntensity);
		p.swapBuffers();
	}
}

void projector_manager::renderProjectorFrusta(ldr_render_pass* renderPass)
{
	static const vec4 colorTable[] =
	{
		vec4(1.f, 0.f, 0.f, 1.f),
		vec4(0.f, 1.f, 0.f, 1.f),
		vec4(0.f, 0.f, 1.f, 1.f),
		vec4(1.f, 1.f, 0.f, 1.f),
		vec4(0.f, 1.f, 1.f, 1.f),
		vec4(1.f, 0.f, 1.f, 1.f),
	};

	uint32 colorIndex = 0;
	for (auto& p : physicalProjectors)
	{
		if (p.active())
		{
			renderCameraFrustum(p.camera, colorTable[colorIndex++ % arraysize(colorTable)], renderPass, 4.f);
		}
	}
	for (auto& p : dummyProjectors)
	{
		if (p.active())
		{
			renderCameraFrustum(p.camera, colorTable[colorIndex++ % arraysize(colorTable)], renderPass, 4.f);
		}
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

