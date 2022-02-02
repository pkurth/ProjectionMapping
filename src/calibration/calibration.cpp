#include "pch.h"
#include "calibration.h"
#include "graycode.h"

#include "core/imgui.h"
#include "core/log.h"
#include "core/image.h"
#include "window/window.h"
#include "window/software_window.h"


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

static const fs::path calibrationBaseDirectory = "calibration_temp";

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
		color_bgra* captures = new color_bgra[maxNumCalibrationPatterns * colorCameraWidth * colorCameraHeight];
		uint32 captureStride = colorCameraWidth * colorCameraHeight;

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

				DirectX::Image image;
				image.width = colorCameraWidth;
				image.height = colorCameraHeight;
				image.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				image.rowPitch = colorCameraWidth * getFormatSize(image.format);
				image.slicePitch = image.rowPitch * image.height;

				for (uint32 g = 0; g < numGrayCodes; ++g)
				{
					image.pixels = (uint8*)(captures + captureStride * g);

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

		delete[] captures;
		delete[] pattern;

		cancel = false;

		state = calibration_state_none;
	});

	thread.detach();

	return true;
}

bool projector_system_calibration::calibrate()
{
	state = calibration_state_calibrating;





	cancel = false;
	state = calibration_state_none;

	return true;
}

projector_system_calibration::projector_system_calibration(depth_tracker* tracker)
{
	if (!tracker || !tracker->camera.isInitialized() || !tracker->camera.colorSensor.active)
	{
		LOG_ERROR("Tracker/camera is not initialized");
		return;
	}

	this->tracker = tracker;
	this->state = calibration_state_none;
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
