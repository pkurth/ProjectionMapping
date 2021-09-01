#include "pch.h"
#include "projector.h"
#include "core/yaml.h"
#include "core/imgui.h"
#include "core/string.h"

physical_projector::physical_projector(const monitor_info& monitor)
{
	this->monitor = monitor;
	this->name = monitor.name;
	//camera.initializeCalibrated(); // TODO
}

void physical_projector::edit()
{
	editCommon(monitor.width, monitor.height);
}

dummy_projector::dummy_projector(std::string name, vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intr)
{
	this->name = name;
	camera.initializeCalibrated(position, rotation, width, height, intr, 0.01f);
	activate();
}

void dummy_projector::edit()
{
	editCommon(camera.width, camera.height);
}

void projector_base::render(const opaque_render_pass* opaqueRenderPass, const directional_light& sun, const ref<pbr_environment>& environment, const render_camera& viewerCamera)
{
	if (!active())
	{
		return;
	}

	camera.setViewport(window.clientWidth, window.clientHeight);
	camera.updateMatrices();

	renderer.beginFrame(camera.width, camera.height);
	renderer.setProjectorCamera(camera);
	renderer.setViewerCamera(viewerCamera);
	renderer.setSun(sun);
	renderer.setEnvironment(environment);
	renderer.submitRenderPass(opaqueRenderPass);
	
	renderer.endFrame();
}

void projector_base::presentToBackBuffer(dx_command_list* cl, bool applySolverIntensity)
{
	renderer.finalizeImage(cl, applySolverIntensity);

	dx_resource backbuffer = window.backBuffers[window.currentBackbufferIndex];
	cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cl->copyResource(renderer.frameResult->resource, backbuffer);
	cl->transitionBarrier(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
}

void projector_base::swapBuffers()
{
	window.swapBuffers();
}

projector_solver_input projector_base::getSolverInput() const
{
	return projector_solver_input{ renderer.frameResult, renderer.worldNormalsTexture, renderer.depthStencilBuffer, renderer.solverIntensity, camera.viewProj, camera.position };
}

void projector_base::editCommon(uint32 width, uint32 height)
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
#ifdef UNICODE
		window.initialize(stringToWstring(name).c_str(), camera.width, camera.height, color_depth_8);
#else
		window.initialize(name.c_str(), camera.width, camera.height, color_depth_8);
#endif
	renderer.initialize(window.colorDepth, camera.width, camera.height);
}

void projector_base::deactivate()
{
	renderer.shutdown();
	window.shutdown();
}
