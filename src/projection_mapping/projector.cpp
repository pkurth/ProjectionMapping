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
	editCommon(monitor.name, monitor.width, monitor.height);
}

dummy_projector::dummy_projector(std::string name, vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intr)
{
	this->name = name;
	camera.initializeCalibrated(position, rotation, width, height, intr, 0.01f);
	activate();
}

void dummy_projector::edit()
{
	editCommon(name, camera.width, camera.height);
}

uint64 projector_base::render(const render_camera& viewerCamera, const opaque_render_pass* opaqueRenderPass)
{
	if (!active())
	{
		return 0;
	}

	directional_light sun;
	sun.direction = normalize(vec3(-0.6f, -1.f, -0.3f));
	sun.color = vec3(1.f, 0.93f, 0.76f);
	sun.intensity = 50.f;
	sun.numShadowCascades = 0;

	camera.setViewport(window.clientWidth, window.clientHeight);
	camera.updateMatrices();

	renderer.beginFrame(camera.width, camera.height);
	renderer.setProjectorCamera(camera);
	renderer.setViewerCamera(viewerCamera);
	renderer.setSun(sun);
	renderer.submitRenderPass(opaqueRenderPass);
	
	renderer.endFrame();


	// We could eventually bundle these into a single command list for all projectors. These are probably too light weight.
	dx_command_list* cl = dxContext.getFreeRenderCommandList();

	dx_resource backbuffer = window.backBuffers[window.currentBackbufferIndex];
	cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cl->copyResource(renderer.frameResult->resource, backbuffer);
	cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

	uint64 result = dxContext.executeCommandList(cl);

	window.swapBuffers();

	return result;
}

void projector_base::editCommon(const std::string& name, uint32 width, uint32 height)
{
	if (ImGui::TreeNode(this, "%s (%ux%u)", name.c_str(), width, height))
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
	renderer.initialize(window.colorDepth, camera.width, camera.height);
}

void projector_base::deactivate()
{
	renderer.shutdown();
	window.shutdown();
}
