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
}

void projector_manager::updateAndRender(const render_camera& viewerCamera)
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
				dummyProjectors.emplace_back("Dummy " + std::to_string(dummyProjectors.size()), vec3(0.f, 0.f, 0.f), quat::identity, 640, 480, camera_intrinsics{ 400.f, 400.f, 320.f, 240.f });
			}

			ImGui::TreePop();
		}
	}

	ImGui::End();


	for (auto& p : physicalProjectors)
	{
		p.render(viewerCamera);
	}
	for (auto& p : dummyProjectors)
	{
		p.render(viewerCamera);
	}

}

