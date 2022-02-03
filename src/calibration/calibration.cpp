#include "pch.h"
#include "calibration.h"
#include "graycode.h"
#include "calibration_internal.h"
#include "point_cloud.h"

#include "core/imgui.h"
#include "core/log.h"
#include "core/image.h"
#include "core/color.h"
#include "window/window.h"
#include "window/software_window.h"

#include "dx/dx_context.h"
#include "dx/dx_render_target.h"
#include "dx/dx_command_list.h"
#include "dx/dx_pipeline.h"

#include "calibration_rs.hlsli"

static const fs::path calibrationBaseDirectory = "calibration_temp";
static dx_pipeline depthToColorPipeline;

static inline std::string getTime()
{
	time_t now = time(0);
	char nowString[100];
	ctime_s(nowString, 100, &now);
	std::string time = nowString;
	std::replace(time.begin(), time.end(), ' ', '_');
	std::replace(time.begin(), time.end(), ':', '.');
	time.pop_back(); // Pop last \n.

	return time;
}

bool projector_system_calibration::projectCalibrationPatterns()
{
	if (tracker->getNumberOfTrackedEntities() == 0)
	{
		return false;
	}

	auto& monitors = win32_window::allConnectedMonitors;

	uint32 totalNumProjectors = 0;
	uint32 numProjectorsToCalibrate = 0;

	int32 maxNumPixels = 0;
	uint32 maxNumCalibrationPatterns = 0;

	for (uint32 i = 0; i < MAX_NUM_PROJECTORS; ++i)
	{
		totalNumProjectors += isProjectorIndex[i];
		numProjectorsToCalibrate += calibrateIndex[i];

		if (calibrateIndex[i])
		{
			maxNumPixels = max(maxNumPixels, monitors[i].width * monitors[i].height);
			maxNumCalibrationPatterns = max(maxNumCalibrationPatterns, getNumberOfGraycodePatternsRequired(monitors[i].width, monitors[i].height));
		}
	}

	mat4 trackingMat = tracker->getTrackingMatrix(0);

	if (numProjectorsToCalibrate == 0)
	{
		return true;
	}

	state = calibration_state_projecting_patterns;


	std::thread thread([=]()
	{
		software_window blackWindows[MAX_NUM_PROJECTORS];
		uint32 numBlackWindows = totalNumProjectors - 1;
		uint8 black = 0;

		for (uint32 i = 0; i < numBlackWindows; ++i)
		{
			if (!blackWindows[i].initialize(L"Black", 1, 1, &black, 1, 1, 1))
			{
				LOG_ERROR("Failed to open black window");
				state = calibration_state_none;
				return;
			}
		}

		tracker->storeColorFrameCopy = true;

		uint32 colorCameraWidth = tracker->camera.colorSensor.width;
		uint32 colorCameraHeight = tracker->camera.colorSensor.height;

		uint8* pattern = new uint8[maxNumPixels];
		uint32 captureStride = colorCameraWidth * colorCameraHeight;
		color_bgra* captures = new color_bgra[maxNumCalibrationPatterns * captureStride];
		uint8* grayCaptures = new uint8[maxNumCalibrationPatterns * captureStride];

		std::string time = getTime();
		fs::path baseDir = calibrationBaseDirectory / time;
		fs::create_directories(baseDir);

		FILE* file = fopen((baseDir / "tracking.txt").string().c_str(), "w+");
		if (file)
		{
			for (int i = 0; i < 16; ++i)
			{
				fprintf(file, "%f ", trackingMat.m[i]);
			}
			fclose(file);
		}

		for (uint32 proj = 0; proj < MAX_NUM_PROJECTORS; ++proj)
		{
			if (calibrateIndex[proj])
			{
				uint32 windowIndex = 0;
				for (uint32 i = 0; i < MAX_NUM_PROJECTORS; ++i)
				{
					if (isProjectorIndex[i] && i != proj)
					{
						software_window& window = blackWindows[windowIndex++];

						if (window.fullscreen)
						{
							window.toggleFullscreen();
						}
						window.moveToMonitor(monitors[i]);
						window.toggleFullscreen();
						window.swapBuffers();
					}
				}

				uint32 width = (uint32)monitors[proj].width;
				uint32 height = (uint32)monitors[proj].height;

				fs::path currentOutputDir = baseDir / monitors[proj].uniqueID;
				fs::create_directories(currentOutputDir);

				software_window patternWindow;
				patternWindow.initialize(L"Pattern", width, height, pattern, 1, width, height);
				patternWindow.moveToMonitor(monitors[proj]);
				patternWindow.toggleFullscreen();

				Sleep(1000);

				uint32 numGrayCodes = getNumberOfGraycodePatternsRequired(width, height);

				for (uint32 g = 0; g < numGrayCodes; ++g)
				{
					color_bgra* colorFrame = captures + captureStride * g;

					generateGraycodePattern(pattern, width, height, g, (uint8)(whiteValue * 255));
					patternWindow.swapBuffers();
					Sleep(500);

					memcpy(colorFrame, tracker->colorFrameCopy, colorCameraWidth * colorCameraHeight * sizeof(color_bgra));

					if (cancel)
					{
						goto cleanup;
					}
				}

				for (uint32 i = 0; i < numGrayCodes * captureStride; ++i)
				{
					color_bgra bgra = captures[i];
					vec3 rgb = { bgra.r / 255.f, bgra.g / 255.f, bgra.b / 255.f };

					rgb = sRGBToLinear(rgb);
					float gray = clamp01(rgb.r * 0.21f + rgb.g * 0.71f + rgb.b * 0.08f);
					gray = linearToSRGB(gray);

					grayCaptures[i] = (uint8)(gray * 255.f);
				}

				DirectX::Image image;
				image.width = colorCameraWidth;
				image.height = colorCameraHeight;
				image.format = DXGI_FORMAT_R8_UNORM;
				image.rowPitch = colorCameraWidth * getFormatSize(image.format);
				image.slicePitch = image.rowPitch * image.height;

				for (uint32 g = 0; g < numGrayCodes; ++g)
				{
					image.pixels = grayCaptures + captureStride * g;

					std::string number = std::to_string(g);
					int length = (int)number.length();
					std::string pad = "";
					for (int j = 0; j < 4 - length; ++j)
					{
						pad += "0";
					}

					fs::path imagePath = currentOutputDir / ("image" + pad + number + ".png");
					saveImageToFile(imagePath, image);

					if (cancel)
					{
						goto cleanup;
					}
				}

				Sleep(1000);
			}
		}


		cleanup:
		
		tracker->storeColorFrameCopy = false;

		delete[] grayCaptures;
		delete[] captures;
		delete[] pattern;

		cancel = false;

		state = calibration_state_none;
	});

	thread.detach();

	return true;
}

struct calibration_sequence
{
	mat4 trackingMat;
	int globalSequenceID;

	std::vector<pixel_correspondence> allPixelCorrespondences;

	calibration_sequence() {}

	calibration_sequence(calibration_sequence&& c)
	{
		trackingMat = c.trackingMat;
		allPixelCorrespondences = std::move(c.allPixelCorrespondences);
		globalSequenceID = c.globalSequenceID;
	}
};

struct proj_calibration_sequence
{
	int screenID;
	std::string uniqueID;
	int width, height;
	std::vector<calibration_sequence> sequences;
};

struct calibration_input
{
	std::vector<proj_calibration_sequence> projectors;
	int32 camWidth, camHeight;
	int32 numGlobalSequences;
};

static bool readMatrixFromFile(const fs::path& filename, mat4& out)
{
	FILE* file = fopen(filename.string().c_str(), "r");
	if (file)
	{
		for (int i = 0; i < 16; ++i)
		{
			if (fscanf(file, "%f", &(out.m[i])) != 1)
			{
				return false;
			}
		}
		fclose(file);
	}
	else
	{
		return false;
	}
	return true;
}

static std::vector<fs::path> findAllSubDirectories(const fs::path& path)
{
	std::vector<fs::path> result;
	for (auto it : fs::directory_iterator(path))
	{
		if (it.is_directory())
		{
			result.push_back(it.path());
		}
	}
	return result;
}

static std::vector<fs::path> findAllFileNames(const fs::path& path, const std::vector<fs::path>& validExtensions)
{
	std::vector<fs::path> result;
	for (auto it : fs::directory_iterator(path))
	{
		if (it.is_regular_file())
		{
			fs::path ext = it.path().extension();

			if (std::find(validExtensions.begin(), validExtensions.end(), ext) != validExtensions.end())
			{
				result.push_back(it.path());
			}
		}
	}
	return result;
}

static bool loadAndDecodeImageSequences(const fs::path& workingDir, const std::vector<monitor_info>& projectors, calibration_input& calibInput)
{
	std::vector<DirectX::ScratchImage> scratchImages;
	std::vector<image<uint8>> imageSequence;
	scratchImages.reserve(100);
	imageSequence.reserve(100);

	calibInput.camWidth = calibInput.camHeight = 0;
	calibInput.projectors.clear();

	std::vector<fs::path> subDirs = findAllSubDirectories(workingDir);

	const std::vector<fs::path> imageExtensions = { ".jpg", ".png" };

	int screenIDToProjID[16];
	for (int i = 0; i < arraysize(screenIDToProjID); ++i)
	{
		screenIDToProjID[i] = -1;
	}

	int globalSequenceID = 0;
	for (const fs::path& sequenceName : subDirs)
	{
		std::vector<fs::path> projDirs = findAllSubDirectories(sequenceName);

		if (projDirs.size() == 0)
		{
			continue;
		}

		const fs::path& trackingFile = sequenceName / "tracking.txt";
		if (!fs::exists(trackingFile))
		{
			LOG_ERROR("Did not find tracking file in sequence '%ws'", sequenceName.c_str());
			continue;
		}

		calibration_sequence calibSequence;
		if (!readMatrixFromFile(trackingFile, calibSequence.trackingMat))
		{
			LOG_ERROR("Could not read tracking matrix for sequence '%ws'", sequenceName.c_str());
			continue;
		}

		int numProjectorsInThisSequence = 0;
		for (fs::path& proj : projDirs)
		{
			fs::path folderName = proj.stem();

			{
				fs::path& uniqueID = folderName;

				int wantedProjector = -1;
				for (int w = 0; w < projectors.size(); ++w)
				{
					if (uniqueID == projectors[w].uniqueID)
					{
						wantedProjector = w;
						break;
					}
				}

				if (wantedProjector == -1)
				{
					continue;
				}

				const monitor_info& projector = projectors[wantedProjector];
				int32 projWidth = projector.width;
				int32 projHeight = projector.height;
				int32 screenID = projector.screenID;

				uint32 expectedNumImages = getNumberOfGraycodePatternsRequired(projWidth, projHeight);

				std::vector<fs::path> sequenceFilenames = findAllFileNames(proj, imageExtensions);
				if (sequenceFilenames.size() != expectedNumImages)
				{
					LOG_ERROR("Directory '%ws' does not contain the expected number of image files. Expected %u, got %u", proj.c_str(), expectedNumImages, (uint32)sequenceFilenames.size());
					continue;
				}

				// This is a valid directory.

				scratchImages.resize(0);
				imageSequence.resize(0);
				for (const fs::path& imageFilename : sequenceFilenames)
				{
					DirectX::ScratchImage scratchImage;
					D3D12_RESOURCE_DESC textureDesc;
					if (!loadImageFromFile(imageFilename, image_load_flags_always_load_from_source, scratchImage, textureDesc))
					{
						LOG_ERROR("Could not load file '%ws'", imageFilename.c_str());
						break;
					}

					if (textureDesc.Format != DXGI_FORMAT_R8_UNORM)
					{
						LOG_ERROR("Image format for file '%ws' does not match expected format DXGI_FORMAT_R8_UNORM", imageFilename.c_str());
						break;
					}

					DirectX::Image dximg = scratchImage.GetImages()[0];

					image<uint8> img = { (uint32)dximg.width, (uint32)dximg.height, dximg.pixels };

					scratchImages.emplace_back(std::move(scratchImage));
					imageSequence.push_back(img);
				}

				if (scratchImages.size() != sequenceFilenames.size())
				{
					continue;
				}

				int camWidth = imageSequence[0].width;
				int camHeight = imageSequence[0].height;

				if (calibInput.camWidth != 0 && calibInput.camHeight != 0)
				{
					if (camWidth != calibInput.camWidth || camHeight != calibInput.camHeight)
					{
						LOG_ERROR("In sequence '%ws' the image dimensions don't match", proj.c_str());
						continue;
					}
				}
				else
				{
					calibInput.camWidth = camWidth;
					calibInput.camHeight = camHeight;
				}

				if (!decodeGraycodeCaptures(imageSequence, projWidth, projHeight, calibSequence.allPixelCorrespondences))
				{
					LOG_ERROR("Could not decode gray code sequence '%ws'", proj.c_str());
					continue;
				}

				int& projID = screenIDToProjID[screenID];
				if (projID == -1)
				{
					projID = (int)calibInput.projectors.size();
				}

				if (projID >= calibInput.projectors.size())
				{
					calibInput.projectors.resize(projID + 1);
				}

				calibSequence.globalSequenceID = globalSequenceID;
				calibInput.projectors[projID].sequences.emplace_back(std::move(calibSequence));
				calibInput.projectors[projID].uniqueID = uniqueID.string();
				calibInput.projectors[projID].screenID = screenID;
				calibInput.projectors[projID].width = projector.width;
				calibInput.projectors[projID].height = projector.height;

				++numProjectorsInThisSequence;
			}
		}

		if (numProjectorsInThisSequence == 0)
		{
			continue;
		}

		++globalSequenceID;
	}

	calibInput.numGlobalSequences = globalSequenceID;

	LOG_MESSAGE("Found %d global input sequences", calibInput.numGlobalSequences);

	return calibInput.projectors.size() > 0;
}

static void projectDepthIntoColorFrame(const ref<composite_mesh>& mesh, 
	const mat4& trackingMat, 
	const mat4& colorCameraViewMat,
	const mat4& colorCameraProjMat, const camera_distortion& colorCameraDistortion,
	ref<dx_texture>& colorBuffer, ref<dx_texture>& depthBuffer, ref<dx_buffer>& readbackBuffer,
	const image<vec2>& colorCameraUnprojectTable,
	image_point_cloud& outPointCloud)
{
	auto renderTarget = dx_render_target(colorBuffer->width, colorBuffer->height)
		.colorAttachment(colorBuffer)
		.depthAttachment(depthBuffer);

	dx_command_list* cl = dxContext.getFreeRenderCommandList();
	cl->transitionBarrier(colorBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);

	cl->setPipelineState(*depthToColorPipeline.pipeline);
	cl->setGraphicsRootSignature(*depthToColorPipeline.rootSignature);

	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cl->setRenderTarget(renderTarget);
	cl->setViewport(renderTarget.viewport);
	cl->clearRTV(colorBuffer, 0.f, 0.f, 0.f, 0.f);
	cl->clearDepth(depthBuffer);

	depth_to_color_cb cb;
	cb.mv = colorCameraViewMat * trackingMat;
	cb.p = colorCameraProjMat;
	cb.distortion = colorCameraDistortion;

	cl->setGraphics32BitConstants(DEPTH_TO_COLOR_RS_CB, cb);

	cl->setVertexBuffer(0, mesh->mesh.vertexBuffer.positions);
	cl->setVertexBuffer(1, mesh->mesh.vertexBuffer.others);
	cl->setIndexBuffer(mesh->mesh.indexBuffer);


	for (auto& submesh : mesh->submeshes)
	{
		cl->drawIndexed(submesh.info.numIndices, 1, submesh.info.firstIndex, submesh.info.baseVertex, 0);
	}

	cl->transitionBarrier(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
	cl->copyTextureRegionToBuffer(colorBuffer, readbackBuffer, 0, 0, 0, colorBuffer->width, colorBuffer->height);

	uint64 fence = dxContext.executeCommandList(cl);

	dxContext.renderQueue.waitForFence(fence);




	image<vec4> output(colorBuffer->width, colorBuffer->height);

	uint8* dest = (uint8*)output.data;
	uint32 destPitch = sizeof(vec4) * output.width;

	uint8* result = (uint8*)mapBuffer(readbackBuffer, true);
	uint32 resultPitch = sizeof(vec4) * alignTo(colorBuffer->width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	for (uint32 h = 0; h < colorBuffer->height; ++h)
	{
		memcpy(dest, result, destPitch);
		result += resultPitch;
		dest += destPitch;
	}

	unmapBuffer(readbackBuffer, false);

	outPointCloud.constructFromRendering(output, colorCameraUnprojectTable);
	outPointCloud.writeToFile(calibrationBaseDirectory / "test.ply");
	outPointCloud.writeToImage(calibrationBaseDirectory / "test.png");
}

bool projector_system_calibration::calibrate()
{
	auto& monitors = win32_window::allConnectedMonitors;

	std::vector<monitor_info> projectors;
	for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
	{
		if (calibrateIndex[i])
		{
			projectors.push_back(monitors[i]);
		}
	}

	if (projectors.size() == 0)
	{
		return false;
	}

	state = calibration_state_calibrating;

	calibration_input calibInput;
	if (!loadAndDecodeImageSequences(calibrationBaseDirectory, projectors, calibInput))
	{
		state = calibration_state_none;
		return false;
	}

	auto& colorSensor = tracker->camera.colorSensor;

	uint32 camWidth = (uint32)calibInput.camWidth;
	uint32 camHeight = (uint32)calibInput.camHeight;

	assert(colorSensor.width == camWidth);
	assert(colorSensor.height == camHeight);

	mat4 colorCameraViewMat = createViewMatrix(colorSensor.position, colorSensor.rotation);
	mat4 colorCameraProjMat = createPerspectiveProjectionMatrix((float)colorSensor.width, (float)colorSensor.height, 
		colorSensor.intrinsics.fx, colorSensor.intrinsics.fy, colorSensor.intrinsics.cx, colorSensor.intrinsics.cy, 0.01f, -1.f);
	camera_distortion colorCameraDistortion = colorSensor.distortion;

	image<vec2> colorCameraUnprojectTable(colorSensor.width, colorSensor.height, colorSensor.unprojectTable);

	proj_calibration_sequence& proj = calibInput.projectors[0];
	calibration_sequence& seq = proj.sequences[0];
	const mat4& trackingMat = seq.trackingMat;

	assert(tracker->getNumberOfTrackedEntities() > 0);

	scene_entity entity = tracker->getTrackedEntity(0);
	ref<composite_mesh> mesh = entity.getComponent<raster_component>().mesh;

	image_point_cloud pointCloud;
	projectDepthIntoColorFrame(mesh, trackingMat, colorCameraViewMat, colorCameraProjMat, colorCameraDistortion, depthToColorTexture, depthBuffer, readbackBuffer, colorCameraUnprojectTable, pointCloud);






	cancel = false;
	state = calibration_state_none;

	return true;
}

projector_system_calibration::projector_system_calibration(depth_tracker* tracker)
{
	if (!tracker || !tracker->camera.isInitialized() || !tracker->camera.depthSensor.active || !tracker->camera.colorSensor.active)
	{
		LOG_ERROR("Tracker/camera is not initialized");
		return;
	}

	this->tracker = tracker;
	this->state = calibration_state_none;


	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.renderTargets(DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_D16_UNORM)
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

		depthToColorPipeline = createReloadablePipeline(desc, { "calibration_depth_to_color_vs", "calibration_depth_to_color_ps"});
	}

	depthToColorTexture = createTexture(0, tracker->camera.colorSensor.width, tracker->camera.colorSensor.height, DXGI_FORMAT_R32G32B32A32_FLOAT, false, true, false, D3D12_RESOURCE_STATE_GENERIC_READ);
	depthBuffer = createDepthTexture(tracker->camera.colorSensor.width, tracker->camera.colorSensor.height, DXGI_FORMAT_D16_UNORM);

	uint32 texturePitch = alignTo(depthToColorTexture->width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	readbackBuffer = createReadbackBuffer(sizeof(vec4), texturePitch * tracker->camera.colorSensor.height);
}

bool projector_system_calibration::edit()
{
	if (state == calibration_state_uninitialized)
	{
		return false;
	}

	bool uiActive = state == calibration_state_none;

	if (state == calibration_state_none)
	{
		cancel = false;
	}

	if (ImGui::BeginTable("##ProjTable", 3, ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("Monitor");
		ImGui::TableSetupColumn("Is projector");
		ImGui::TableSetupColumn("Calibrate");

		ImGui::TableHeadersRow();

		auto& monitors = win32_window::allConnectedMonitors;
		for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
		{
			ImGui::PushID(i);

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::Text(monitors[i].description.c_str());

			ImGui::TableNextColumn();
			ImGui::DisableableCheckbox("##isProj", isProjectorIndex[i], uiActive);

			if (!isProjectorIndex[i])
			{
				calibrateIndex[i] = false;
			}

			ImGui::TableNextColumn();
			ImGui::DisableableCheckbox("##calib", calibrateIndex[i], uiActive && isProjectorIndex[i]);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::Separator();

	if (ImGui::DisableableButton("Clear temp data", uiActive))
	{
		fs::remove_all(calibrationBaseDirectory);
	}

	if (ImGui::BeginProperties())
	{
		if (!uiActive)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PropertySlider("White value", whiteValue);
		if (!uiActive)
		{
			ImGui::EndDisabled();
		}



		auto cancelableButton = [](const char* label, calibration_state state, calibration_state runningState, volatile bool& cancel)
		{
			if (state == runningState)
			{
				if (ImGui::PropertyButton(label, "Cancel"))
				{
					cancel = true;
				}
			}
			else
			{
				if (ImGui::PropertyDisableableButton(label, "Go", state == calibration_state_none))
				{
					return true;
				}
			}

			return false;
		};

		if (cancelableButton("Project calibration patterns", state, calibration_state_projecting_patterns, cancel))
		{
			projectCalibrationPatterns();
		}

		if (cancelableButton("Calibrate", state, calibration_state_calibrating, cancel))
		{
			calibrate();
		}

		ImGui::EndProperties();
	}

	return true;
}
