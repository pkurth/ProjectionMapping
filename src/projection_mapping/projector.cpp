#include "pch.h"
#include "projector.h"
#include "core/yaml.h"
#include "core/imgui.h"

physical_projector::physical_projector(const monitor_info& monitor)
{
	this->monitor = monitor;
	//camera.initializeCalibrated(); // TODO
}

void physical_projector::edit()
{
	if (ImGui::TreeNode(this, "%s (%ux%u)", monitor.name.c_str(), monitor.width, monitor.height))
	{
		bool active = this->active();
		if (ImGui::Checkbox("Active", &active))
		{
			if (active)
			{
				activate();
			}
			else
			{
				deactivate();
			}
		}

		ImGui::TreePop();
	}
}

dummy_projector::dummy_projector(std::string name, vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intr)
{
	this->name = name;
	camera.initializeCalibrated(position, rotation, width, height, intr, 0.01f);
}

void dummy_projector::edit()
{
	if (ImGui::TreeNode(this, name.c_str()))
	{
		bool active = this->active();
		if (ImGui::Checkbox("Active", &active))
		{
			if (active)
			{
				activate();
			}
			else
			{
				deactivate();
			}
		}

		ImGui::TreePop();
	}
}

void projector_base::activate()
{
	window.initialize(TEXT("Projector"), camera.width, camera.height, color_depth_8);
}

void projector_base::deactivate()
{
	window.shutdown();
}
