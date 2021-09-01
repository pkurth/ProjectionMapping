#include "pch.h"
#include "projector_manager.h"
#include "window/dx_window.h"
#include "core/imgui.h"


projector_manager::projector_manager()
{
	monitors = getAllDisplayDevices();

	for (auto& monitor : monitors)
	{
		physicalProjectors.emplace_back(monitor);
	}

	addDummyProjector();
	addDummyProjector();
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


	for (auto& p : physicalProjectors)
	{
		p.render(opaqueRenderPass, sun, environment, viewerCamera);
	}
	for (auto& p : dummyProjectors)
	{
		p.render(opaqueRenderPass, sun, environment, viewerCamera);
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

