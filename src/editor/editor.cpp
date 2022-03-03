#include "pch.h"
#include "editor.h"
#include "core/imgui.h"
#include "core/cpu_profiling.h"
#include "core/log.h"
#include "dx/dx_profiling.h"
#include "scene/components.h"
#include "animation/animation.h"
#include "geometry/mesh.h"
#include "scene/serialization.h"
#include "projection_mapping/projector.h"
#include "rendering/debug_visualization.h"
#include "audio/audio.h"

#include <fontawesome/list.h>

struct transform_undo
{
	scene_entity entity;
	trs before;
	trs after;

	void undo() { entity.getComponent<transform_component>() = before; }
	void redo() { entity.getComponent<transform_component>() = after; }
};

struct selection_undo
{
	scene_editor* editor;
	scene_entity before;
	scene_entity after;

	void undo() { editor->setSelectedEntityNoUndo(before); }
	void redo() { editor->setSelectedEntityNoUndo(after); }
};

struct sun_direction_undo
{
	directional_light* sun;
	vec3 before;
	vec3 after;

	void undo() { sun->direction = before; }
	void redo() { sun->direction = after; }
};

void scene_editor::updateSelectedEntityUIRotation()
{
	if (selectedEntity)
	{
		quat rotation = quat::identity;

		if (transform_component* transform = selectedEntity.getComponentIfExists<transform_component>())
		{
			rotation = transform->rotation;
		}
		else if (position_rotation_component* prc = selectedEntity.getComponentIfExists<position_rotation_component>())
		{
			rotation = prc->rotation;
		}

		selectedEntityEulerRotation = quatToEuler(rotation);
		selectedEntityEulerRotation.x = rad2deg(angleToZeroToTwoPi(selectedEntityEulerRotation.x));
		selectedEntityEulerRotation.y = rad2deg(angleToZeroToTwoPi(selectedEntityEulerRotation.y));
		selectedEntityEulerRotation.z = rad2deg(angleToZeroToTwoPi(selectedEntityEulerRotation.z));
	}
}

void scene_editor::setSelectedEntity(scene_entity entity)
{
	if (selectedEntity != entity)
	{
		undoStack.pushAction("selection", selection_undo{ this, selectedEntity, entity });
	}

	setSelectedEntityNoUndo(entity);
}

void scene_editor::setSelectedEntityNoUndo(scene_entity entity)
{
	selectedEntity = entity;
	updateSelectedEntityUIRotation();
}

void scene_editor::initialize(game_scene* scene, main_renderer* renderer, depth_tracker* tracker, projector_manager* projectorManager, projector_system_calibration* projectorCalibration)
{
	this->scene = scene;
	this->renderer = renderer;
	this->tracker = tracker;
	this->projectorManager = projectorManager;
	this->projectorCalibration = projectorCalibration;
	cameraController.initialize(&scene->camera);

	systemInfo = getSystemInfo();
}

bool scene_editor::update(const user_input& input, ldr_render_pass* ldrRenderPass, float dt)
{
	CPU_PROFILE_BLOCK("Update editor");

	if (selectedEntity && !selectedEntity.isValid())
	{
		setSelectedEntityNoUndo({});
	}

	bool objectDragged = false;
	objectDragged |= handleUserInput(input, ldrRenderPass, dt);
	objectDragged |= drawSceneHierarchy();
	drawHardwareWindow();
	drawMainMenuBar();
	drawSettings(dt);

	for (auto [entityHandle, projector] : scene->view<projector_component>().each())
	{
		if (!projector.window.open && !projector.headless)
		{
			LOG_MESSAGE("Deleting projector");

			scene_entity entity = { entityHandle, *scene };
			scene->deleteEntity(entity);
			setSelectedEntity({});
			break;
		}
	}

	return objectDragged;
}

void scene_editor::drawMainMenuBar()
{
	static bool showIconsWindow = false;
	static bool showDemoWindow = false;
	static bool showSystemWindow = false;

	bool controlsClicked = false;
	bool aboutClicked = false;
	bool systemClicked = false;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(ICON_FA_FILE "  File"))
		{
			char textBuffer[128];
			snprintf(textBuffer, sizeof(textBuffer), ICON_FA_UNDO " Undo %s", undoStack.undoPossible() ? undoStack.getUndoName() : "");
			if (ImGui::MenuItem(textBuffer, "Ctrl+Z", false, undoStack.undoPossible()))
			{
				undoStack.undo();
			}

			snprintf(textBuffer, sizeof(textBuffer), ICON_FA_REDO " Redo %s", undoStack.redoPossible() ? undoStack.getRedoName() : "");
			if (ImGui::MenuItem(textBuffer, "Ctrl+Y", false, undoStack.redoPossible()))
			{
				undoStack.redo();
			}
			ImGui::Separator();

			if (ImGui::MenuItem(ICON_FA_SAVE "  Save scene", "Ctrl+S"))
			{
				serializeToFile();
			}

			if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Load scene", "Ctrl+O", nullptr, projectorManager->isNetworkServer()))
			{
				deserializeFromFile();
			}

			if (ImGui::MenuItem(ICON_FA_BARCODE "  Load projector calibrations", "Ctrl+Shift+O"))
			{
				deserializeProjectorCalibrationsFromFile();
			}

			ImGui::Separator();
			if (ImGui::MenuItem(ICON_FA_TIMES "  Exit", "Esc"))
			{
				PostQuitMessage(0);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(ICON_FA_TOOLS "  Developer"))
		{
			if (ImGui::MenuItem(showIconsWindow ? (ICON_FA_ICONS "  Hide available icons") : (ICON_FA_ICONS "  Show available icons")))
			{
				showIconsWindow = !showIconsWindow;
			}

			if (ImGui::MenuItem(showDemoWindow ? (ICON_FA_PUZZLE_PIECE "  Hide demo window") : (ICON_FA_PUZZLE_PIECE "  Show demo window")))
			{
				showDemoWindow = !showDemoWindow;
			}

			ImGui::Separator();

			if (ImGui::MenuItem(dxProfilerWindowOpen ? (ICON_FA_CHART_BAR "  Hide GPU profiler") : (ICON_FA_CHART_BAR "  Show GPU profiler"), nullptr, nullptr, ENABLE_DX_PROFILING))
			{
				dxProfilerWindowOpen = !dxProfilerWindowOpen;
			}

			if (ImGui::MenuItem(cpuProfilerWindowOpen ? (ICON_FA_CHART_LINE "  Hide CPU profiler") : (ICON_FA_CHART_LINE "  Show CPU profiler"), nullptr, nullptr, ENABLE_CPU_PROFILING))
			{
				cpuProfilerWindowOpen = !cpuProfilerWindowOpen;
			}

			ImGui::Separator();

			if (ImGui::MenuItem(logWindowOpen ? (ICON_FA_CLIPBOARD_LIST "  Hide message log") : (ICON_FA_CLIPBOARD_LIST "  Show message log"), "Ctrl+L", nullptr, ENABLE_MESSAGE_LOG))
			{
				logWindowOpen = !logWindowOpen;
			}

			ImGui::Separator();

			if (ImGui::MenuItem(ICON_FA_DESKTOP "  System"))
			{
				systemClicked = true;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(ICON_FA_CHILD "  Help"))
		{
			if (ImGui::MenuItem(ICON_FA_COMPASS "  Controls"))
			{
				controlsClicked = true;
			}

			if (ImGui::MenuItem(ICON_FA_QUESTION "  About"))
			{
				aboutClicked = true;
			}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();

	if (systemClicked)
	{
		ImGui::OpenPopup(ICON_FA_DESKTOP "  System");
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(ICON_FA_DESKTOP "  System", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Value("CPU", systemInfo.cpuName.c_str());
		ImGui::Value("GPU", systemInfo.gpuName.c_str());

		ImGui::Separator();

		ImGui::PopupOkButton();
		ImGui::EndPopup();
	}

	if (controlsClicked)
	{
		ImGui::OpenPopup(ICON_FA_COMPASS "  Controls");
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(ICON_FA_COMPASS "  Controls", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("The camera can be controlled in two modes:");
		ImGui::BulletText(
			"Free flying: Hold the right mouse button and move the mouse to turn.\n"
			"Move with WASD (while holding right mouse). Q & E let you move down and up.\n"
			"Holding Shift will make you fly faster, Ctrl will make you slower."
		);
		ImGui::BulletText(
			"Orbit: While holding Alt, press and hold the left mouse button to\n"
			"orbit around a point in front of the camera. Hold the middle mouse button \n"
			"to pan."
		);
		ImGui::Separator();
		ImGui::Text(
			"Left-click on objects to select them. Toggle through gizmos using\n"
			"Q (no gizmo), W (translate), E (rotate), R (scale).\n"
			"Press G to toggle between global and local coordinate system.\n"
			"You can also change the object's transform in the Scene Hierarchy window."
		);
		ImGui::Separator();
		ImGui::Text(
			"Press F to focus the camera on the selected object. This automatically\n"
			"sets the orbit distance such that you now orbit around this object (with alt, see above)."
		);
		ImGui::Separator();
		ImGui::Text(
			"Press V to toggle Vsync on or off."
		);
		ImGui::Separator();
		ImGui::Text(
			"You can drag and drop meshes from the asset window at the bottom into the scene\n"
			"window to add it to the scene."
		);
		ImGui::Separator();

		ImGui::PopupOkButton();
		ImGui::EndPopup();
	}

	if (aboutClicked)
	{
		ImGui::OpenPopup(ICON_FA_QUESTION "  About");
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(ICON_FA_QUESTION "  About", 0, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Direct3D renderer");
		ImGui::Separator();

		ImGui::PopupOkButton();
		ImGui::EndPopup();
	}


	if (showIconsWindow)
	{
		ImGui::Begin(ICON_FA_ICONS "  Icons", &showIconsWindow);

		static ImGuiTextFilter filter;
		filter.Draw();

		ImGui::BeginChild("Icons List");
		for (uint32 i = 0; i < arraysize(awesomeIcons); ++i)
		{
			ImGui::PushID(i);
			if (filter.PassFilter(awesomeIconNames[i]))
			{
				ImGui::Text("%s: %s", awesomeIconNames[i], awesomeIcons[i]);
				ImGui::SameLine();
				if (ImGui::Button("Copy to clipboard"))
				{
					ImGui::SetClipboardText(awesomeIconNames[i]);
				}
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::End();
	}

	if (showDemoWindow)
	{
		ImGui::ShowDemoWindow(&showDemoWindow);
	}
}

template<typename component_t, typename ui_func>
static void drawComponent(scene_entity entity, const char* componentName, ui_func func)
{
	const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
	if (auto* component = entity.getComponentIfExists<component_t>())
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
		bool open = ImGui::TreeNodeEx(componentName, treeNodeFlags, componentName);
		ImGui::PopStyleVar();

		if (open)
		{
			func(*component);
			ImGui::TreePop();
		}
	}
}

bool scene_editor::drawSceneHierarchy()
{
	game_scene& scene = *this->scene;

	bool objectMovedByWidget = false;

	if (ImGui::Begin("Scene Hierarchy"))
	{
		if (ImGui::BeginChild("Outliner", ImVec2(0, 250)))
		{
			scene.view<tag_component>()
				.each([this, &scene](auto entityHandle, tag_component& tag)
			{
				const char* name = tag.name;
				scene_entity entity = { entityHandle, scene };

				if (entity == selectedEntity)
				{
					ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), name);
				}
				else
				{
					ImGui::Text(name);
				}

				if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1))
				{
					setSelectedEntity(entity);
				}

				bool entityDeleted = false;
				if (ImGui::BeginPopupContextItem(name))
				{
					if (ImGui::MenuItem("Delete"))
					{
						entityDeleted = true;
					}

					ImGui::EndPopup();
				}

				if (entityDeleted)
				{
					scene.deleteEntity(entity);
					setSelectedEntityNoUndo({});
				}
			});
		}
		ImGui::EndChild();
		ImGui::Separator();

		if (selectedEntity)
		{
			ImGui::Dummy(ImVec2(0, 20));
			ImGui::Separator();
			ImGui::Separator();
			ImGui::Separator();

			if (ImGui::BeginChild("Components"))
			{
				ImGui::AlignTextToFramePadding();

				ImGui::PushID((uint32)selectedEntity);
				ImGui::InputText("Name", selectedEntity.getComponent<tag_component>().name, sizeof(tag_component::name));
				ImGui::PopID();
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_TRASH_ALT))
				{
					scene.deleteEntity(selectedEntity);
					setSelectedEntityNoUndo({});
					objectMovedByWidget = true;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Delete entity");
				}

				if (selectedEntity)
				{
					drawComponent<transform_component>(selectedEntity, "TRANSFORM", [this, &objectMovedByWidget](transform_component& transform)
					{
						if (ImGui::BeginProperties())
						{
							objectMovedByWidget |= ImGui::PropertyDrag("Position", transform.position, 0.1f);

							if (ImGui::PropertyDrag("Rotation", selectedEntityEulerRotation, 0.1f))
							{
								vec3 euler = selectedEntityEulerRotation;
								euler.x = deg2rad(euler.x);
								euler.y = deg2rad(euler.y);
								euler.z = deg2rad(euler.z);
								transform.rotation = eulerToQuat(euler);

								objectMovedByWidget = true;
							}

							objectMovedByWidget |= ImGui::PropertyDrag("Scale", transform.scale, 0.1f);

							if (ImGui::PropertyButton("Export", "Copy 4x4 matrix to clipboard"))
							{
								mat4 m = trsToMat4(transform);

								char buffer[512];
								snprintf(buffer, sizeof(buffer),
									"%f %f %f %f "
									"%f %f %f %f "
									"%f %f %f %f "
									"%f %f %f %f",
									m.m[0], m.m[1], m.m[2], m.m[3],
									m.m[4], m.m[5], m.m[6], m.m[7],
									m.m[8], m.m[9], m.m[10], m.m[11],
									m.m[12], m.m[13], m.m[14], m.m[15]);
								
								ImGui::SetClipboardText(buffer);
							}

							if (ImGui::PropertyButton("Import", "Set from clipboard"))
							{
								mat4 m;

								const char* buffer = ImGui::GetClipboardText();
								sscanf(buffer,
									"%f %f %f %f "
									"%f %f %f %f "
									"%f %f %f %f "
									"%f %f %f %f",
									&m.m[0], &m.m[1], &m.m[2], &m.m[3],
									&m.m[4], &m.m[5], &m.m[6], &m.m[7],
									&m.m[8], &m.m[9], &m.m[10], &m.m[11],
									&m.m[12], &m.m[13], &m.m[14], &m.m[15]);

								transform = mat4ToTRS(m);
							}

							ImGui::EndProperties();
						}
					});

					drawComponent<position_component>(selectedEntity, "TRANSFORM", [&objectMovedByWidget](position_component& position)
					{
						if (ImGui::BeginProperties())
						{
							objectMovedByWidget |= ImGui::PropertyDrag("Position", position.position, 0.1f);
							ImGui::EndProperties();
						}
					});

					drawComponent<position_rotation_component>(selectedEntity, "TRANSFORM", [this, &objectMovedByWidget](position_rotation_component& pr)
					{
						if (ImGui::BeginProperties())
						{
							objectMovedByWidget |= ImGui::PropertyDrag("Translation", pr.position, 0.1f);
							if (ImGui::PropertyDrag("Rotation", selectedEntityEulerRotation, 0.1f))
							{
								vec3 euler = selectedEntityEulerRotation;
								euler.x = deg2rad(euler.x);
								euler.y = deg2rad(euler.y);
								euler.z = deg2rad(euler.z);
								pr.rotation = eulerToQuat(euler);

								objectMovedByWidget = true;
							}
							ImGui::EndProperties();
						}
					});

					drawComponent<dynamic_transform_component>(selectedEntity, "DYNAMIC", [](dynamic_transform_component& dynamic)
					{
						ImGui::Text("Dynamic");
					});

					drawComponent<tracking_component>(selectedEntity, "TRACKING", [this](tracking_component& tracking)
					{
						if (tracking.trackingData && ImGui::BeginProperties())
						{
							const transform_component& transform = selectedEntity.getComponent<transform_component>();

							ImGui::PropertyCheckbox("Tracking", tracking.trackingData->tracking);

							if (ImGui::PropertyButton("Export", "Copy relative 4x4 matrix to clipboard",
								"Copies the transform relative to the tracker. If the tracker is rotated, this is taken into account."))
							{
								mat4 m = tracker->getTrackingMatrix(transform);

								char buffer[512];
								snprintf(buffer, sizeof(buffer),
									"%ff, %ff, %ff, %ff, "
									"%ff, %ff, %ff, %ff, "
									"%ff, %ff, %ff, %ff, "
									"%ff, %ff, %ff, %ff",
									m.m[0], m.m[1], m.m[2], m.m[3],
									m.m[4], m.m[5], m.m[6], m.m[7],
									m.m[8], m.m[9], m.m[10], m.m[11],
									m.m[12], m.m[13], m.m[14], m.m[15]);

								ImGui::SetClipboardText(buffer);
							}

							if (ImGui::PropertyButton("Global orientation", "Rotate world to match",
								"Rotates the world such that this object stands upright"))
							{
								quat delta = rotateFromTo(transform.rotation * vec3(0.f, 1.f, 0.f), vec3(0.f, 1.f, 0.f));;
								tracker->globalCameraRotation = delta * tracker->globalCameraRotation;

								// No need to set the tracker dummy object, since this is handled by transforming the position_rotation_components below.

								for (auto [entityHandle, transform] : this->scene->view<transform_component>().each())
								{
									transform.position = delta * transform.position;
									transform.rotation = delta * transform.rotation;
								}

								for (auto [entityHandle, transform] : this->scene->view<position_rotation_component>().each())
								{
									transform.position = delta * transform.position;
									transform.rotation = delta * transform.rotation;
								}

								for (auto [entityHandle, transform] : this->scene->view<position_component>().each())
								{
									transform.position = delta * transform.position;
								}

								for (auto& calib : projectorManager->context.knownProjectorCalibrations)
								{
									calib.second.position = delta * calib.second.position;
									calib.second.rotation = delta * calib.second.rotation;
								}

								projectorManager->onSceneLoad();
							}

							ImGui::PropertyValue("Number of correspondences", tracking.trackingData->numCorrespondences);

							ImGui::EndProperties();
						}
					});

					drawComponent<animation_component>(selectedEntity, "ANIMATION", [this](animation_component& anim)
					{
						if (raster_component* raster = selectedEntity.getComponentIfExists<raster_component>())
						{
							if (ImGui::BeginProperties())
							{
								uint32 animationIndex = anim.animation.clip ? (uint32)(anim.animation.clip - raster->mesh->skeleton.clips.data()) : -1;

								bool animationChanged = ImGui::PropertyDropdown("Currently playing", [](uint32 index, void* data)
								{
									if (index == -1) { return "---"; }

									animation_skeleton& skeleton = *(animation_skeleton*)data;
									const char* result = 0;
									if (index < (uint32)skeleton.clips.size())
									{
										result = skeleton.clips[index].name.c_str();
									}
									return result;
								}, animationIndex, &raster->mesh->skeleton);

								if (animationChanged)
								{
									anim.animation.set(&raster->mesh->skeleton.clips[animationIndex]);
								}

								ImGui::EndProperties();
							}
						}
					});

					drawComponent<point_light_component>(selectedEntity, "POINT LIGHT", [](point_light_component& pl)
					{
						if (ImGui::BeginProperties())
						{
							ImGui::PropertyColor("Color", pl.color);
							ImGui::PropertySlider("Intensity", pl.intensity, 0.f, 10.f);
							ImGui::PropertySlider("Radius", pl.radius, 0.f, 100.f);
							ImGui::PropertyCheckbox("Casts shadow", pl.castsShadow);
							if (pl.castsShadow)
							{
								ImGui::PropertyDropdownPowerOfTwo("Shadow resolution", 128, 2048, pl.shadowMapResolution);
							}

							ImGui::EndProperties();
						}
					});

					drawComponent<spot_light_component>(selectedEntity, "SPOT LIGHT", [](spot_light_component& sl)
					{
						if (ImGui::BeginProperties())
						{
							float inner = rad2deg(sl.innerAngle);
							float outer = rad2deg(sl.outerAngle);

							ImGui::PropertyColor("Color", sl.color);
							ImGui::PropertySlider("Intensity", sl.intensity, 0.f, 10.f);
							ImGui::PropertySlider("Distance", sl.distance, 0.f, 100.f);
							ImGui::PropertySlider("Inner angle", inner, 0.1f, 80.f);
							ImGui::PropertySlider("Outer angle", outer, 0.2f, 85.f);
							ImGui::PropertyCheckbox("Casts shadow", sl.castsShadow);
							if (sl.castsShadow)
							{
								ImGui::PropertyDropdownPowerOfTwo("Shadow resolution", 128, 2048, sl.shadowMapResolution);
							}

							sl.innerAngle = deg2rad(inner);
							sl.outerAngle = deg2rad(outer);

							ImGui::EndProperties();
						}
					});

					drawComponent<projector_component>(selectedEntity, "PROJECTOR", [](projector_component& p)
					{
						if (ImGui::BeginProperties())
						{
							ImGui::PropertyValue("Intrinsics", *(vec4*)&p.intrinsics);

							uint32 monitorIndex = p.window.monitorIndex;

							bool monitorChanged = ImGui::PropertyDropdown("Monitor", [](uint32 index, void* data) -> const char*
							{
								if (index >= (uint32)win32_window::allConnectedMonitors.size()) { return 0; }
								return win32_window::allConnectedMonitors[index].description.c_str();
							}, monitorIndex, 0);

							if (monitorChanged)
							{
								p.window.moveToMonitor(win32_window::allConnectedMonitors[monitorIndex]);
							}

							bool fullscreen = p.window.fullscreen;
							if (ImGui::PropertyCheckbox("Fullscreen", fullscreen))
							{
								p.window.toggleFullscreen();
							}

							ImGui::EndProperties();
						}
					});


					if (!selectedEntity.hasComponent<tracking_component>() && selectedEntity.hasComponent<raster_component>() && selectedEntity.hasComponent<transform_component>())
					{
						ImGui::Separator();
						if (ImGui::Button("Track this object"))
						{
							selectedEntity.addComponent<tracking_component>();
						}
					}

				}
			}
			ImGui::EndChild();
		}
	}
	ImGui::End();

	return objectMovedByWidget;
}

void scene_editor::drawHardwareWindow()
{
	if (ImGui::Begin("Hardware"))
	{
		if (ImGui::BeginTree("Monitors/Projectors"))
		{
			for (const auto& monitor : win32_window::allConnectedMonitors)
			{
				ImGui::PushID(&monitor);
				if (ImGui::BeginTree(monitor.description.c_str()))
				{
					if (ImGui::BeginProperties())
					{
						ImGui::PropertyInputText("Unique ID", (char*)monitor.uniqueID.c_str(), 128, true);

						ImGui::EndProperties();
					}
					ImGui::EndTree();
				}
				ImGui::PopID();
			}
			ImGui::EndTree();
		}

		if (ImGui::BeginTree("Depth cameras"))
		{
			for (const auto& cam : rgbd_camera::allConnectedRGBDCameras)
			{
				ImGui::PushID(&cam);
				if (ImGui::BeginTree(cam.description.c_str()))
				{
					if (ImGui::BeginProperties())
					{
						ImGui::PropertyValue("Type", rgbdCameraTypeNames[cam.type]);
						ImGui::PropertyValue("Index", cam.deviceIndex);
						ImGui::PropertyValue("Serial number", cam.serialNumber.c_str());

						ImGui::EndProperties();
					}
					ImGui::EndTree();
				}
				ImGui::PopID();
			}

			ImGui::EndTree();
		}
	}
	ImGui::End();
}

bool scene_editor::handleUserInput(const user_input& input, ldr_render_pass* ldrRenderPass, float dt)
{
	// Returns true, if the user dragged an object using a gizmo.

	if (input.keyboard['F'].pressEvent && selectedEntity && selectedEntity.hasComponent<transform_component>())
	{
		auto& transform = selectedEntity.getComponent<transform_component>();

		auto aabb = selectedEntity.hasComponent<raster_component>() ? selectedEntity.getComponent<raster_component>().mesh->aabb : bounding_box::fromCenterRadius(0.f, 1.f);
		aabb.minCorner *= transform.scale;
		aabb.maxCorner *= transform.scale;

		aabb.minCorner += transform.position;
		aabb.maxCorner += transform.position;

		cameraController.centerCameraOnObject(aabb);
	}

	bool inputCaptured = cameraController.update(input, renderer->renderWidth, renderer->renderHeight, dt);

	bool objectMovedByGizmo = false;


	if (selectedEntity)
	{
		if (transform_component* transform = selectedEntity.getComponentIfExists<transform_component>())
		{
			bool draggingBefore = gizmo.dragging;

			if (gizmo.manipulateTransformation(*transform, scene->camera, input, !inputCaptured, ldrRenderPass))
			{
				updateSelectedEntityUIRotation();
				inputCaptured = true;
				objectMovedByGizmo = true;
			}
			else
			{
				if (draggingBefore)
				{
					undoStack.pushAction("transform entity", transform_undo{ selectedEntity, gizmo.originalTransform, *transform });
				}
			}
		}
		else if (position_component* pc = selectedEntity.getComponentIfExists<position_component>())
		{
			if (gizmo.manipulatePosition(pc->position, scene->camera, input, !inputCaptured, ldrRenderPass))
			{
				inputCaptured = true;
			}
		}
		else if (position_rotation_component* prc = selectedEntity.getComponentIfExists<position_rotation_component>())
		{
			if (gizmo.manipulatePositionRotation(prc->position, prc->rotation, scene->camera, input, !inputCaptured, ldrRenderPass))
			{
				inputCaptured = true;
			}
		}
		else
		{
			gizmo.manipulateNothing(scene->camera, input, !inputCaptured, ldrRenderPass);
		}

		if (!inputCaptured && !ImGui::IsAnyItemActive())
		{
			if (ImGui::IsKeyPressed(key_backspace) || ImGui::IsKeyPressed(key_delete))
			{
				// Delete entity.
				scene->deleteEntity(selectedEntity);
				setSelectedEntity({});
				inputCaptured = true;
				objectMovedByGizmo = true;
			}
			else if (ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('D'))
			{
				// Duplicate entity.
				scene_entity newEntity = scene->copyEntity(selectedEntity);
				setSelectedEntity(newEntity);
				inputCaptured = true;
				objectMovedByGizmo = true;
			}
		}
	}
	else
	{
		gizmo.manipulateNothing(scene->camera, input, !inputCaptured, ldrRenderPass);
	}


	if (!inputCaptured && !ImGui::IsAnyItemActive() && ImGui::IsKeyDown(key_shift) && ImGui::IsKeyPressed('A'))
	{
		ImGui::OpenPopup("CreateEntityPopup");
		inputCaptured = true;
	}

	drawEntityCreationPopup();

	if (ImGui::BeginControlsWindow("##EntityControls"))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

		if (ImGui::Button(ICON_FA_PLUS)) { ImGui::OpenPopup("CreateEntityPopup"); }
		if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Create entity (Shift+A)"); }
		drawEntityCreationPopup();

		ImGui::PopStyleColor();
	}

	ImGui::End();


	if (!ImGui::IsAnyItemActive())
	{
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('Z'))
		{
			undoStack.undo();
			inputCaptured = true;
			objectMovedByGizmo = true;
		}
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('Y'))
		{
			undoStack.redo();
			inputCaptured = true;
			objectMovedByGizmo = true;
		}
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('S'))
		{
			serializeToFile();
			inputCaptured = true;
			ImGui::GetIO().KeysDown['S'] = false; // Hack: Window does not get notified of inputs due to the file dialog.
		}
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('O') && projectorManager->isNetworkServer())
		{
			deserializeFromFile();
			inputCaptured = true;
			ImGui::GetIO().KeysDown['O'] = false; // Hack: Window does not get notified of inputs due to the file dialog.
		}
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyDown(key_shift) && ImGui::IsKeyPressed('O'))
		{
			deserializeProjectorCalibrationsFromFile();
			inputCaptured = true;
			ImGui::GetIO().KeysDown['O'] = false; // Hack: Window does not get notified of inputs due to the file dialog.
		}
		if (!inputCaptured && ImGui::IsKeyDown(key_ctrl) && ImGui::IsKeyPressed('L'))
		{
			logWindowOpen = !logWindowOpen;
			inputCaptured = true;
		}
	}

	if (!inputCaptured && input.mouse.left.clickEvent)
	{
		if (input.keyboard[key_ctrl].down)
		{
			vec3 dir = -scene->camera.generateWorldSpaceRay(input.mouse.relX, input.mouse.relY).direction;
			undoStack.pushAction("sun direction", sun_direction_undo{ &scene->sun, scene->sun.direction, dir });
			scene->sun.direction = dir;
			inputCaptured = true;
		}
		else
		{
			if (renderer->hoveredObjectID != -1)
			{
				setSelectedEntity({ renderer->hoveredObjectID, *scene });
			}
			else
			{
				setSelectedEntity({});
			}
		}
		inputCaptured = true;
	}

	return objectMovedByGizmo;
}

void scene_editor::drawEntityCreationPopup()
{
	if (ImGui::BeginPopup("CreateEntityPopup"))
	{
		bool clicked = false;

		if (ImGui::MenuItem("Point light", "P") || ImGui::IsKeyPressed('P'))
		{
			auto pl = scene->createEntity("Point light")
				.addComponent<position_component>(scene->camera.position + scene->camera.rotation * vec3(0.f, 0.f, -3.f))
				.addComponent<point_light_component>(
					vec3(1.f, 1.f, 1.f),
					1.f,
					10.f,
					false,
					512u
				);

			setSelectedEntity(pl);
			clicked = true;
		}

		if (ImGui::MenuItem("Spot light", "S") || ImGui::IsKeyPressed('S'))
		{
			auto sl = scene->createEntity("Spot light")
				.addComponent<position_rotation_component>(scene->camera.position + scene->camera.rotation * vec3(0.f, 0.f, -3.f), quat::identity)
				.addComponent<spot_light_component>(
					vec3(1.f, 1.f, 1.f),
					1.f,
					25.f,
					deg2rad(20.f),
					deg2rad(30.f),
					false,
					512u
				);

			setSelectedEntity(sl);
			clicked = true;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Projector", "B") || ImGui::IsKeyPressed('B'))
		{
			auto projector = scene->createEntity("Projector")
				.addComponent<position_rotation_component>(scene->camera.position + scene->camera.rotation * vec3(0.f, 0.f, -1.f), scene->camera.rotation)
				.addComponent<projector_component>();

			setSelectedEntity(projector);
			clicked = true;
		}

		if (clicked)
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void scene_editor::setEnvironment(const fs::path& filename)
{
	scene->environment = createEnvironment(filename); // Currently synchronous (on render queue).
	renderer->pathTracer.resetRendering();

	if (!scene->environment)
	{
		LOG_WARNING("Could not load environment '%ws'. Renderer will use procedural sky box. Procedural sky boxes currently cannot contribute to global illumination, so expect very dark lighting", filename.c_str());
		std::cout << "Could not load environment '" << filename << "'. Renderer will use procedural sky box. Procedural sky boxes currently cannot contribute to global illumination, so expect very dark lighting.\n";
	}
}

void scene_editor::serializeToFile()
{
	serializeSceneToDisk(*scene, renderer->settings, tracker, &projectorManager->context);
}

bool scene_editor::deserializeFromFile()
{
	std::string environmentName;
	if (deserializeSceneFromDisk(*scene, renderer->settings, environmentName, tracker, &projectorManager->context))
	{
		setSelectedEntityNoUndo({});
		setEnvironment(environmentName);

		projectorManager->onSceneLoad();

		return true;
	}
	return false;
}

bool scene_editor::deserializeProjectorCalibrationsFromFile()
{
	projector_context context;
	if (deserializeProjectorCalibrationOnly(&context, tracker->globalCameraPosition, tracker->globalCameraRotation))
	{
		setSelectedEntityNoUndo({});

		projectorManager->reportLocalCalibration(context.knownProjectorCalibrations);

		return true;
	}
	return false;
}

static bool editCamera(render_camera& camera)
{
	bool result = false;
	if (ImGui::BeginTree("Camera"))
	{
		if (ImGui::BeginProperties())
		{
			result |= ImGui::PropertySliderAngle("Field of view", camera.verticalFOV, 1.f, 150.f);
			result |= ImGui::PropertyInput("Near plane", camera.nearPlane);
			bool infiniteFarplane = camera.farPlane < 0.f;
			if (ImGui::PropertyCheckbox("Infinite far plane", infiniteFarplane))
			{
				if (!infiniteFarplane)
				{
					camera.farPlane = (camera.farPlane == -1.f) ? 500.f : -camera.farPlane;
				}
				else
				{
					camera.farPlane = -camera.farPlane;
				}
				result = true;
			}
			if (!infiniteFarplane)
			{
				result |= ImGui::PropertyInput("Far plane", camera.farPlane);
			}

			ImGui::EndProperties();
		}
		
		ImGui::EndTree();
	}
	return result;
}

static bool plotAndEditTonemapping(tonemap_settings& tonemap)
{
	bool result = false;
	if (ImGui::BeginTree("Tonemapping"))
	{
		ImGui::PlotLines("",
			[](void* data, int idx)
		{
			float t = idx * 0.01f;
			tonemap_settings& aces = *(tonemap_settings*)data;
			return aces.tonemap(t);
		},
			&tonemap, 100, 0, 0, 0.f, 1.f, ImVec2(250.f, 250.f));

		if (ImGui::BeginProperties())
		{
			result |= ImGui::PropertySlider("Shoulder strength", tonemap.A, 0.f, 1.f);
			result |= ImGui::PropertySlider("Linear strength", tonemap.B, 0.f, 1.f);
			result |= ImGui::PropertySlider("Linear angle", tonemap.C, 0.f, 1.f);
			result |= ImGui::PropertySlider("Toe strength", tonemap.D, 0.f, 1.f);
			result |= ImGui::PropertySlider("Tone numerator", tonemap.E, 0.f, 1.f);
			result |= ImGui::PropertySlider("Toe denominator", tonemap.F, 0.f, 1.f);
			result |= ImGui::PropertySlider("Linear white", tonemap.linearWhite, 0.f, 100.f);
			result |= ImGui::PropertySlider("Exposure", tonemap.exposure, -3.f, 3.f);
			ImGui::EndProperties();
		}

		ImGui::EndTree();
	}
	return result;
}

static bool editSunShadowParameters(directional_light& sun)
{
	bool result = false;
	if (ImGui::BeginTree("Sun"))
	{
		if (ImGui::BeginProperties())
		{
			result |= ImGui::PropertySlider("Intensity", sun.intensity, 0.f, 1000.f);
			result |= ImGui::PropertyColor("Color", sun.color);

			result |= ImGui::PropertyDropdownPowerOfTwo("Shadow resolution", 128, 2048, sun.shadowDimensions);
			result |= ImGui::PropertyCheckbox("Stabilize", sun.stabilize);

			result |= ImGui::PropertySlider("# Cascades", sun.numShadowCascades, 1, 4);

			const float minCascadeDistance = 0.f, maxCascadeDistance = 300.f;
			const float minBias = 0.f, maxBias = 0.0015f;
			const float minBlend = 0.f, maxBlend = 10.f;
			if (sun.numShadowCascades == 1)
			{
				result |= ImGui::PropertySlider("Distance", sun.cascadeDistances.x, minCascadeDistance, maxCascadeDistance);
				result |= ImGui::PropertySlider("Bias", sun.bias.x, minBias, maxBias, "%.6f");
				result |= ImGui::PropertySlider("Blend distances", sun.blendDistances.x, minBlend, maxBlend, "%.6f");
			}
			else if (sun.numShadowCascades == 2)
			{
				result |= ImGui::PropertySlider("Distance", sun.cascadeDistances.xy, minCascadeDistance, maxCascadeDistance);
				result |= ImGui::PropertySlider("Bias", sun.bias.xy, minBias, maxBias, "%.6f");
				result |= ImGui::PropertySlider("Blend distances", sun.blendDistances.xy, minBlend, maxBlend, "%.6f");
			}
			else if (sun.numShadowCascades == 3)
			{
				result |= ImGui::PropertySlider("Distance", sun.cascadeDistances.xyz, minCascadeDistance, maxCascadeDistance);
				result |= ImGui::PropertySlider("Bias", sun.bias.xyz, minBias, maxBias, "%.6f");
				result |= ImGui::PropertySlider("Blend distances", sun.blendDistances.xyz, minBlend, maxBlend, "%.6f");
			}
			else if (sun.numShadowCascades == 4)
			{
				result |= ImGui::PropertySlider("Distance", sun.cascadeDistances, minCascadeDistance, maxCascadeDistance);
				result |= ImGui::PropertySlider("Bias", sun.bias, minBias, maxBias, "%.6f");
				result |= ImGui::PropertySlider("Blend distances", sun.blendDistances, minBlend, maxBlend, "%.6f");
			}

			ImGui::EndProperties();
		}

		ImGui::EndTree();
	}
	return result;
}

static bool editAO(bool& enable, hbao_settings& settings, const ref<dx_texture>& aoTexture)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable HBAO", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Num rays", settings.numRays, 1, 16);
			result |= ImGui::PropertySlider("Max num steps per ray", settings.maxNumStepsPerRay, 1, 16);
			result |= ImGui::PropertySlider("Radius", settings.radius, 0.f, 1.f, "%.3fm");
			result |= ImGui::PropertySlider("Strength", settings.strength, 0.f, 2.f);
		}
		ImGui::EndProperties();
	}
	if (enable && aoTexture && ImGui::BeginTree("Show##ShowAO"))
	{
		ImGui::Image(aoTexture);
		ImGui::EndTree();
	}
	return result;
}

static bool editSSS(bool& enable, sss_settings& settings, const ref<dx_texture>& sssTexture)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable SSS", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Num iterations", settings.numSteps, 1, 64);
			result |= ImGui::PropertySlider("Ray distance", settings.rayDistance, 0.05f, 3.f, "%.3fm");
			result |= ImGui::PropertySlider("Thickness", settings.thickness, 0.05f, 1.f, "%.3fm");
			result |= ImGui::PropertySlider("Max distance from camera", settings.maxDistanceFromCamera, 5.f, 1000.f, "%.3fm");
			result |= ImGui::PropertySlider("Distance fadeout range", settings.distanceFadeoutRange, 1.f, 5.f, "%.3fm");
			result |= ImGui::PropertySlider("Border fadeout", settings.borderFadeout, 0.f, 0.5f);
		}
		ImGui::EndProperties();
	}
	if (enable && sssTexture && ImGui::BeginTree("Show##ShowSSS"))
	{
		ImGui::Image(sssTexture);
		ImGui::EndTree();
	}
	return result;
}

static bool editSSR(bool& enable, ssr_settings& settings, const ref<dx_texture>& ssrTexture)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable SSR", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Num iterations", settings.numSteps, 1, 1024);
			result |= ImGui::PropertySlider("Max distance", settings.maxDistance, 5.f, 1000.f, "%.3fm");
			result |= ImGui::PropertySlider("Min stride", settings.minStride, 1.f, 50.f, "%.3fm");
			result |= ImGui::PropertySlider("Max stride", settings.maxStride, settings.minStride, 50.f, "%.3fm");
		}
		ImGui::EndProperties();
	}
	if (enable && ssrTexture && ImGui::BeginTree("Show##ShowSSR"))
	{
		ImGui::Image(ssrTexture);
		ImGui::EndTree();
	}
	return result;
}

static bool editTAA(bool& enable, taa_settings& settings)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable TAA", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Jitter strength", settings.cameraJitterStrength);
		}
		ImGui::EndProperties();
	}
	return result;
}

static bool editBloom(bool& enable, bloom_settings& settings, const ref<dx_texture>& bloomTexture)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable bloom", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Bloom threshold", settings.threshold, 0.5f, 100.f);
			result |= ImGui::PropertySlider("Bloom strength", settings.strength);
		}
		ImGui::EndProperties();
	}
	if (enable && bloomTexture && ImGui::BeginTree("Show##ShowBloom"))
	{
		ImGui::Image(bloomTexture);
		ImGui::EndTree();
	}
	return result;
}

static bool editSharpen(bool& enable, sharpen_settings& settings)
{
	bool result = false;
	if (ImGui::BeginProperties())
	{
		result |= ImGui::PropertyCheckbox("Enable sharpen", enable);
		if (enable)
		{
			result |= ImGui::PropertySlider("Sharpen strength", settings.strength);
		}
		ImGui::EndProperties();
	}
	return result;
}

void scene_editor::drawSettings(float dt)
{
	if (ImGui::Begin("Settings"))
	{
		path_tracer& pathTracer = renderer->pathTracer;

		ImGui::Text("%.3f ms, %u FPS", dt * 1000.f, (uint32)(1.f / dt));

		if (ImGui::BeginProperties())
		{
			if (ImGui::PropertyDropdown("Renderer mode", rendererModeNames, renderer_mode_count, (uint32&)renderer->mode))
			{
				pathTracer.resetRendering();
			}

			dx_memory_usage memoryUsage = dxContext.getMemoryUsage();

			ImGui::PropertyValue("Video memory usage", "%u / %uMB", memoryUsage.currentlyUsed, memoryUsage.available);
			//ImGui::PropertyValue("Running command lists", "%u", dxContext.renderQueue.numRunningCommandLists);

			ImGui::PropertyDropdown("Aspect ratio", aspectRatioNames, aspect_ratio_mode_count, (uint32&)renderer->aspectRatioMode);

			ImGui::PropertyCheckbox("Static shadow map caching", enableStaticShadowMapCaching);

			ImGui::EndProperties();
		}

		editCamera(scene->camera);
		plotAndEditTonemapping(renderer->settings.tonemapSettings);
		editSunShadowParameters(scene->sun);

		if (ImGui::BeginTree("Post processing"))
		{
			if (renderer->spec.allowAO) { editAO(renderer->settings.enableAO, renderer->settings.aoSettings, renderer->getAOResult()); ImGui::Separator(); }
			if (renderer->spec.allowSSS) { editSSS(renderer->settings.enableSSS, renderer->settings.sssSettings, renderer->getSSSResult()); ImGui::Separator(); }
			if (renderer->spec.allowSSR) { editSSR(renderer->settings.enableSSR, renderer->settings.ssrSettings, renderer->getSSRResult()); ImGui::Separator(); }
			if (renderer->spec.allowTAA) { editTAA(renderer->settings.enableTAA, renderer->settings.taaSettings); ImGui::Separator(); }
			if (renderer->spec.allowBloom) { editBloom(renderer->settings.enableBloom, renderer->settings.bloomSettings, renderer->getBloomResult()); ImGui::Separator(); }
			editSharpen(renderer->settings.enableSharpen, renderer->settings.sharpenSettings);

			ImGui::EndTree();
		}

		if (ImGui::BeginTree("Environment"))
		{
			if (ImGui::BeginProperties())
			{
				ImGui::PropertySlider("Environment intensity", renderer->settings.environmentIntensity, 0.f, 2.f);
				ImGui::PropertySlider("Sky intensity", renderer->settings.skyIntensity, 0.f, 2.f);
				ImGui::EndProperties();
			}

			ImGui::EndTree();
		}

		if (ImGui::BeginTree("Tracker"))
		{
			tracker->drawSettings();
			ImGui::EndTree();
		}

		if (ImGui::BeginTree("Calibration"))
		{
			projectorCalibration->edit(*scene);
			ImGui::EndTree();
		}

		if (ImGui::BeginTree("Audio"))
		{
			bool change = false;
			if (ImGui::BeginProperties())
			{
				change |= ImGui::PropertyDrag("Master volume", audio::masterVolume, 0.05f);
				ImGui::EndProperties();
			}
			if (change)
			{
				audio::notifyOnSettingsChange();
			}
			ImGui::EndTree();
		}

		if (renderer->mode == renderer_mode_pathtraced)
		{
			bool pathTracerDirty = false;
			if (ImGui::BeginProperties())
			{
				pathTracerDirty |= ImGui::PropertySlider("Max recursion depth", pathTracer.recursionDepth, 0, pathTracer.maxRecursionDepth - 1);
				pathTracerDirty |= ImGui::PropertySlider("Start russian roulette after", pathTracer.startRussianRouletteAfter, 0, pathTracer.recursionDepth);
				pathTracerDirty |= ImGui::PropertyCheckbox("Use thin lens camera", pathTracer.useThinLensCamera);
				if (pathTracer.useThinLensCamera)
				{
					pathTracerDirty |= ImGui::PropertySlider("Focal length", pathTracer.focalLength, 0.5f, 50.f);
					pathTracerDirty |= ImGui::PropertySlider("F-Number", pathTracer.fNumber, 1.f, 128.f);
				}
				pathTracerDirty |= ImGui::PropertyCheckbox("Use real materials", pathTracer.useRealMaterials);
				pathTracerDirty |= ImGui::PropertyCheckbox("Enable direct lighting", pathTracer.enableDirectLighting);
				if (pathTracer.enableDirectLighting)
				{
					pathTracerDirty |= ImGui::PropertySlider("Light intensity scale", pathTracer.lightIntensityScale, 0.f, 50.f);
					pathTracerDirty |= ImGui::PropertySlider("Point light radius", pathTracer.pointLightRadius, 0.01f, 1.f);

					pathTracerDirty |= ImGui::PropertyCheckbox("Multiple importance sampling", pathTracer.multipleImportanceSampling);
				}

				ImGui::EndProperties();
			}


			if (pathTracerDirty)
			{
				pathTracer.numAveragedFrames = 0;
			}
		}
	}

	ImGui::End();
}




