#include "pch.h"
#include "calibration.h"
#include "graycode.h"
#include "point_cloud.h"
#include "fundamental.h"
#include "svd.h"
#include "reconstruction.h"
#include "solver.h"
#include "solver_ceres.h"

#include "core/imgui.h"
#include "core/log.h"
#include "core/image.h"
#include "core/color.h"
#include "core/cpu_profiling.h"
#include "core/string.h"

#include "editor/file_dialog.h"

#include "window/window.h"
#include "window/software_window.h"

#include "dx/dx_context.h"
#include "dx/dx_render_target.h"
#include "dx/dx_command_list.h"
#include "dx/dx_profiling.h"
#include "dx/dx_pipeline.h"

#include "rendering/material.h"
#include "rendering/render_command.h"
#include "rendering/render_utils.h"
#include "rendering/render_resources.h"
#include "rendering/debug_visualization.h"

#include "calibration_rs.hlsli"

#include <random>
#include <shellapi.h>

static fs::path calibrationBaseDirectory = "calibration_temp";

static dx_pipeline depthToColorPipeline;
static dx_pipeline visualizePointCloudPipeline;



static constexpr uint32 MAX_NUM_PROJECTORS = projector_manager::MAX_NUM_PROJECTORS;

bool projector_system_calibration::projectCalibrationPatterns(game_scene& scene)
{
	auto group = scene.group(entt::get<tracking_component, raster_component, transform_component>);

	if (group.size() == 0)
	{
		return false;
	}

	auto& monitors = win32_window::allConnectedMonitors;

	uint32 totalNumProjectors = 0;

	int32 maxNumPixels = 0;
	uint32 maxNumCalibrationPatterns = 0;

	for (uint32 i = 0; i < MAX_NUM_PROJECTORS; ++i)
	{
		totalNumProjectors += manager->isProjectorIndex[i];

		if (manager->isProjectorIndex[i])
		{
			maxNumPixels = max(maxNumPixels, monitors[i].width * monitors[i].height);
			maxNumCalibrationPatterns = max(maxNumCalibrationPatterns, getNumberOfGraycodePatternsRequired(monitors[i].width, monitors[i].height));
		}
	}

	scene_entity entity = { *group.begin(), scene };
	const transform_component& transform = entity.getComponent<transform_component>();
	const tracking_component& tracking = entity.getComponent<tracking_component>();
	if (!tracking.trackingData || tracking.trackingData->numCorrespondences < 1000)
	{
		LOG_ERROR("Object isn't tracked, cancelling calibration");
		return false;
	}

	mat4 trackingMat = tracker->getTrackingMatrix(transform);

	if (totalNumProjectors == 0)
	{
		return true;
	}

	software_window* patternWindow = new software_window;

	uint8 dummyPattern = 0;

	if (!patternWindow->initialize(L"Pattern", 1, 1, &dummyPattern, 1, 1, 1))
	{
		LOG_ERROR("Failed to open pattern window");
		delete[] patternWindow;
		return false;
	}

	auto oldMode = manager->solver.settings.mode;
	manager->solver.settings.mode = projector_mode_calibration;

	patternWindow->setAlwaysOnTop();


	state = calibration_state_projecting_patterns;

	std::thread thread([=]()
	{
		tracker->storeColorFrameCopy = true;

		uint32 colorCameraWidth = tracker->camera.colorSensor.width;
		uint32 colorCameraHeight = tracker->camera.colorSensor.height;

		uint8* pattern = new uint8[maxNumPixels];
		uint32 captureStride = colorCameraWidth * colorCameraHeight;
		color_bgra* captures = new color_bgra[maxNumCalibrationPatterns * captureStride];
		uint8* grayCaptures = new uint8[maxNumCalibrationPatterns * captureStride];

		std::string time = getTimeString();
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
			if (manager->isProjectorIndex[proj])
			{
				uint32 width = (uint32)monitors[proj].width;
				uint32 height = (uint32)monitors[proj].height;

				fs::path currentOutputDir = baseDir / monitors[proj].uniqueID;
				fs::create_directories(currentOutputDir);

				patternWindow->setNewBuffer(pattern, 1, width, height);
				if (patternWindow->fullscreen)
				{
					patternWindow->toggleFullscreen();
				}
				patternWindow->moveToMonitor(monitors[proj]);
				patternWindow->toggleFullscreen();

				Sleep(3000);

				uint32 numGrayCodes = getNumberOfGraycodePatternsRequired(width, height);

				for (uint32 g = 0; g < numGrayCodes; ++g)
				{
					color_bgra* colorFrame = captures + captureStride * g;

					generateGraycodePattern(pattern, width, height, g, (uint8)(whiteValue * 255));
					patternWindow->swapBuffers();
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

				LOG_MESSAGE("Saving %u calibration images to directory '%ws'", numGrayCodes, currentOutputDir.c_str());

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

				LOG_MESSAGE("Saved %u calibration images to directory '%ws'", numGrayCodes, currentOutputDir.c_str());
			}
		}


		cleanup:
		
		tracker->storeColorFrameCopy = false;

		delete[] grayCaptures;
		delete[] captures;
		delete[] pattern;

		mutex.lock();
		windowsToClose.push_back(patternWindow);
		mutex.unlock();

		cancel = false;
		state = calibration_state_none;
		manager->solver.settings.mode = oldMode;
	});

	HANDLE handle = (HANDLE)thread.native_handle();
	SetThreadDescription(handle, L"Calibration pattern thread");

	thread.detach();

	return true;
}

struct calibration_sequence
{
	mat4 trackingMat;
};

struct calibration_proj_sequence
{
	uint32 sequenceID;

	image<vec2> perPixelCorrespondences;
	std::vector<pixel_correspondence> allPixelCorrespondences;
};

struct calibration_projector
{
	std::string uniqueID;
	int width, height;

	std::vector<calibration_proj_sequence> sequences;
};

struct calibration_input
{
	int32 camWidth, camHeight;

	std::vector<calibration_sequence> sequences;
	std::vector<calibration_projector> projectors;
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

	if (fs::exists(path))
	{
		for (auto it : fs::directory_iterator(path))
		{
			if (it.is_directory())
			{
				result.push_back(it.path());
			}
		}
	}
	return result;
}

static std::vector<fs::path> findAllImagesInDirectory(const fs::path& path)
{
	std::vector<fs::path> result;

	if (fs::exists(path))
	{
		for (auto it : fs::directory_iterator(path))
		{
			if (it.is_regular_file())
			{
				fs::path ext = it.path().extension();

				if (isImageExtension(ext))
				{
					result.push_back(it.path());
				}
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

	calibInput.projectors.clear();
	calibInput.sequences.clear();

	calibInput.camWidth = 0;
	calibInput.camHeight = 0;

	std::vector<fs::path> subDirs = findAllSubDirectories(workingDir);

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
		for (const fs::path& proj : projDirs)
		{
			fs::path uniqueID = proj.stem();

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

			uint32 expectedNumImages = getNumberOfGraycodePatternsRequired(projWidth, projHeight);

			std::vector<fs::path> sequenceFilenames = findAllImagesInDirectory(proj);
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


			calibration_proj_sequence projSequence;
			projSequence.sequenceID = (uint32)calibInput.sequences.size();

			if (!decodeGraycodeCaptures(imageSequence, projWidth, projHeight, projSequence.perPixelCorrespondences, projSequence.allPixelCorrespondences))
			{
				LOG_ERROR("Could not decode gray code sequence '%ws'", proj.c_str());
				continue;
			}


			auto projIt = std::find_if(calibInput.projectors.begin(), calibInput.projectors.end(), [&uniqueID](const calibration_projector& p) { return p.uniqueID == uniqueID; });
			if (projIt == calibInput.projectors.end())
			{
				projIt = calibInput.projectors.insert(calibInput.projectors.end(), { uniqueID.string(), projWidth, projHeight });
			}

			projIt->sequences.emplace_back(std::move(projSequence));
			++numProjectorsInThisSequence;
		}

		if (numProjectorsInThisSequence != 0)
		{
			calibInput.sequences.emplace_back(std::move(calibSequence));
		}
	}

	LOG_MESSAGE("Found %u global input sequences", (uint32)calibInput.sequences.size());

	return calibInput.sequences.size() > 0;
}

static image_point_cloud projectDepthIntoColorFrame(const ref<composite_mesh>& mesh,
	const mat4& trackingMat, 
	const mat4& colorCameraViewMat,
	const mat4& colorCameraProjMat, const camera_distortion& colorCameraDistortion,
	ref<dx_texture>& colorBuffer, ref<dx_texture>& depthBuffer, ref<dx_buffer>& readbackBuffer,
	const image<vec2>& colorCameraUnprojectTable)
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
	uint32 resultPitch = (uint32)alignTo(sizeof(vec4) * colorBuffer->width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	for (uint32 h = 0; h < colorBuffer->height; ++h)
	{
		memcpy(dest, result, destPitch);
		result += resultPitch;
		dest += destPitch;
	}

	unmapBuffer(readbackBuffer, false);

	image_point_cloud pc;
	pc.constructFromRendering(output, colorCameraUnprojectTable);
	return pc;
}


static mat3d cameraMatrix(const camera_intrinsics& intrinsics)
{
	mat3d result = mat3d::identity;
	result.m00 = intrinsics.fx;
	result.m11 = intrinsics.fy;
	result.m02 = intrinsics.cx;
	result.m12 = intrinsics.cy;
	return result;
}

static mat3d switchCoordinateSystem(const mat3d& m)
{
	mat3d r;
	r.m00 = m.m00;
	r.m11 = m.m11;
	r.m12 = m.m12;
	r.m21 = m.m21;
	r.m22 = m.m22;
	r.m10 = -m.m10;
	r.m20 = -m.m20;
	r.m01 = -m.m01;
	r.m02 = -m.m02;
	return r;
}

static vec3d switchCoordinateSystem(const vec3d& v)
{
	return vec3d{ v.x, -v.y, -v.z };
}

static bool testRotationTranslationCombination(const camera_intrinsics& camIntrinsics, uint32 camWidth, uint32 camHeight, 
	const camera_intrinsics& projIntrinsics, uint32 projWidth, uint32 projHeight, 
	quat r, vec3 t, const std::vector<pixel_correspondence>& pc)
{
	quat rotation = conjugate(r);
	vec3 origin = -(rotation * t);

#if 0
	const uint32 numTests = (uint32)pc.size();

	bool result = false;

	for (uint32 i = 0; i < numTests; ++i)
	{
		vec3 pCS, pPS;

		vec2 camPixel = pc[i].camera;
		vec2 projPixel = pc[i].projector;
		float distance;

		pCS = triangulateStereo(camIntrinsics, projIntrinsics, origin, rotation, camPixel, projPixel, distance);
		pPS = r * pCS + t;

		bool inFront = pCS.z < 0.f && pPS.z < 0.f;

		if (i > 0 && result != inFront)
		{
			std::cerr << "Something is wrong!" << std::endl;
		}
		result = inFront;
	}
#else

	float distance;
	vec3 pCS = triangulateStereo(camIntrinsics, projIntrinsics, origin, rotation, vec2(camWidth * 0.5f, camHeight * 0.5f), vec2(projWidth * 0.5f, projHeight * 0.5f), distance, triangulate_center_point);
	vec3 pPS = r * pCS + t;

	bool result = pCS.z < 0.f && pPS.z < 0.f;

#endif
	return result;
}

bool projector_system_calibration::computeInitialExtrinsicProjectorCalibrationEstimate(
	std::vector<pixel_correspondence> pixelCorrespondences,
	const image_point_cloud& renderedPointCloud,
	const camera_intrinsics& camIntrinsics, uint32 camWidth, uint32 camHeight, 
	const camera_intrinsics& projIntrinsics, uint32 projWidth, uint32 projHeight, 
	vec3& outPosition, quat& outRotation)
{
#if 0
	std::cout << "std::vector<pixel_correspondence> pixelCorrespondences = { \n";
	for (auto& pc : pixelCorrespondences)
	{
		std::cout << "{ { " << pc.camera.x << ", " << pc.camera.y << " }, { " << pc.projector.x << ", " << pc.projector.y << " } }, \n";
	}
	std::cout << "};\n";
#endif

	std::vector<uint8> mask;
	mat3d fundamentalMat = computeFundamentalMatrix(pixelCorrespondences, mask);

	uint32 maskedNumCorrespondingPoints = 0;

	// Sort out outliers.
	for (int i = 0, j = 0; i < pixelCorrespondences.size(); ++i)
	{
		if (mask[i] == 1)
		{
			pixelCorrespondences[j] = pixelCorrespondences[i];
			++j;
			++maskedNumCorrespondingPoints;
		}
	}

	pixelCorrespondences.resize(maskedNumCorrespondingPoints);

	mat3d projK = cameraMatrix(projIntrinsics);
	mat3d camK = cameraMatrix(camIntrinsics);

	mat3d essentialMat = transpose(projK) * fundamentalMat * camK;

	svd3 svd = computeSVD(essentialMat);

	const mat3d& u = svd.U;
	mat3d vt = transpose(svd.V);

	mat3d W(0, -1, 0, 1, 0, 0, 0, 0, 1);		// HZ 9.13
	mat3d rotation1 = (u * W * vt);				// HZ 9.19
	mat3d rotation2 = (u * transpose(W) * vt);	// HZ 9.19

	vec3d translation1 = col(u, 2);
	vec3d translation2 = -translation1;


	if ((determinant(rotation1) > 0) != (determinant(rotation2) > 0))
	{
		LOG_ERROR("One matrix is mirror and the other is not (determinants don't match)");
		return false;
	}

	if (determinant(rotation1) < 0)
	{
		rotation1 = rotation1 * -1.f;
		rotation2 = rotation2 * -1.f; // This seems to always work, but is this the correct way to do this?
	}

	quatd ourRotation1d = mat3ToQuaternion(switchCoordinateSystem(rotation1));
	quatd ourRotation2d = mat3ToQuaternion(switchCoordinateSystem(rotation2));
	vec3d ourTranslation1d = switchCoordinateSystem(translation1);
	vec3d ourTranslation2d = switchCoordinateSystem(translation2);

	quat ourRotation1 = { (float)ourRotation1d.x, (float)ourRotation1d.y, (float)ourRotation1d.z, (float)ourRotation1d.w };
	quat ourRotation2 = { (float)ourRotation2d.x, (float)ourRotation2d.y, (float)ourRotation2d.z, (float)ourRotation2d.w };
	vec3 ourTranslation1 = { (float)ourTranslation1d.x, (float)ourTranslation1d.y, (float)ourTranslation1d.z };
	vec3 ourTranslation2 = { (float)ourTranslation2d.x, (float)ourTranslation2d.y, (float)ourTranslation2d.z };

	// Determine correct combination of rotation and translation.
	int combinationIndex = 0;
	combinationIndex |= (int)testRotationTranslationCombination(camIntrinsics, camWidth, camHeight, projIntrinsics, projWidth, projHeight, ourRotation1, ourTranslation1, pixelCorrespondences) << 0;
	combinationIndex |= (int)testRotationTranslationCombination(camIntrinsics, camWidth, camHeight, projIntrinsics, projWidth, projHeight, ourRotation1, ourTranslation2, pixelCorrespondences) << 1;
	combinationIndex |= (int)testRotationTranslationCombination(camIntrinsics, camWidth, camHeight, projIntrinsics, projWidth, projHeight, ourRotation2, ourTranslation1, pixelCorrespondences) << 2;
	combinationIndex |= (int)testRotationTranslationCombination(camIntrinsics, camWidth, camHeight, projIntrinsics, projWidth, projHeight, ourRotation2, ourTranslation2, pixelCorrespondences) << 3;

	quat estimatedRot;
	vec3 estimatedTransDirection;
	switch (combinationIndex)
	{
		case 1:
			estimatedRot = ourRotation1;
			estimatedTransDirection = ourTranslation1;
			break;
		case 2:
			estimatedRot = ourRotation1;
			estimatedTransDirection = ourTranslation2;
			break;
		case 4:
			estimatedRot = ourRotation2;
			estimatedTransDirection = ourTranslation1;
			break;
		case 8:
			estimatedRot = ourRotation2;
			estimatedTransDirection = ourTranslation2;
			break;
		default:
			LOG_ERROR("More or fewer than one combination is valid");
			return false;
	}


	//submitFrustumForVisualization(-(conjugate(ourRotation1) * ourTranslation1), conjugate(ourRotation1), projWidth, projHeight, projIntrinsics, combinationIndex & 1 ? vec4(0.f, 1.f, 0.f, 1.f) : vec4(1.f, 0.f, 0.f, 1.f));
	//submitFrustumForVisualization(-(conjugate(ourRotation1) * ourTranslation2), conjugate(ourRotation1), projWidth, projHeight, projIntrinsics, combinationIndex & 2 ? vec4(0.f, 1.f, 0.f, 1.f) : vec4(1.f, 0.f, 0.f, 1.f));
	//submitFrustumForVisualization(-(conjugate(ourRotation2) * ourTranslation1), conjugate(ourRotation2), projWidth, projHeight, projIntrinsics, combinationIndex & 4 ? vec4(0.f, 1.f, 0.f, 1.f) : vec4(1.f, 0.f, 0.f, 1.f));
	//submitFrustumForVisualization(-(conjugate(ourRotation2) * ourTranslation2), conjugate(ourRotation2), projWidth, projHeight, projIntrinsics, combinationIndex & 8 ? vec4(0.f, 1.f, 0.f, 1.f) : vec4(1.f, 0.f, 0.f, 1.f));


	quat rotation = conjugate(estimatedRot);
	vec3 origin = -(rotation * estimatedTransDirection);

	LOG_MESSAGE("Initial unscaled estimated projector origin: [%.3f, %.3f, %.3f]", origin.x, origin.y, origin.z);
	LOG_MESSAGE("Initial estimated projector rotation: [%.3f, %.3f, %.3f, %.3f]", rotation.x, rotation.y, rotation.z, rotation.w);





	// Compute scale.

	float scale = 0.f;

	vec3 normOrigin = normalize(origin);

	for (const auto& pc : pixelCorrespondences)
	{
		vec3 world = renderedPointCloud.entries((int)pc.camera.y, (int)pc.camera.x).position;
		assert(world.z != 0.f);

		vec3 camRay = normalize(unproject(pc.camera, camIntrinsics));
		vec3 projRay = normalize(rotation * unproject(pc.projector, projIntrinsics));

		float camAngle = acos(clamp(dot(camRay, normOrigin), -1.f, 1.f));		// Alpha.
		float projAngle = acos(clamp(dot(projRay, -normOrigin), -1.f, 1.f));	// Beta.
		float worldAngle = M_PI - camAngle - projAngle;							// Gamma.

		float worldLength = length(world);

		float lambda = worldLength / sin(projAngle);
		float s = lambda * sin(worldAngle);

		scale += s;
	}

	scale /= (float)pixelCorrespondences.size();

	origin *= scale;

	LOG_MESSAGE("Scaling origin by %.3f. Scaled origin: [%.3f, %.3f, %.3f]", scale, origin.x, origin.y, origin.z);

	//submitFrustumForVisualization(origin, rotation, projWidth, projHeight, projIntrinsics, vec4(0.f, 0.f, 1.f, 1.f));

	outRotation = rotation;
	outPosition = origin;

	return true;
}

bool projector_system_calibration::calibrate(game_scene& scene)
{
	auto& monitors = win32_window::allConnectedMonitors;

	std::vector<monitor_info> projectors;
	for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
	{
		if (manager->isProjectorIndex[i])
		{
			projectors.push_back(monitors[i]);
		}
	}

	if (projectors.size() == 0)
	{
		return false;
	}

	auto group = scene.group(entt::get<tracking_component, raster_component, transform_component>);
	if (group.size() == 0)
	{
		return false;
	}

	scene_entity entity = { *group.begin(), scene };
	ref<composite_mesh> mesh = entity.getComponent<raster_component>().mesh;


	state = calibration_state_calibrating;


	std::thread thread([this, projectors, mesh]()
	{
		calibration_input calibInput;
		if (!loadAndDecodeImageSequences(calibrationBaseDirectory, projectors, calibInput))
		{
			state = calibration_state_none;
			return;
		}

		if (cancel)
		{
			cancel = false;
			state = calibration_state_none;
			return;
		}

		auto& camera = tracker->camera.colorSensor;

		uint32 camWidth = (uint32)calibInput.camWidth;
		uint32 camHeight = (uint32)calibInput.camHeight;

		assert(camera.width == camWidth);
		assert(camera.height == camHeight);

		camera_intrinsics camIntrinsics = camera.intrinsics;
		camera_distortion camDistortion = camera.distortion;

		mat4 colorCameraViewMat = createViewMatrix(camera.position, camera.rotation);
		mat4 colorCameraProjMat = createPerspectiveProjectionMatrix((float)camWidth, (float)camHeight,
			camIntrinsics.fx, camIntrinsics.fy, camIntrinsics.cx, camIntrinsics.cy, 0.01f, -1.f);
		
		image<vec2> colorCameraUnprojectTable(camWidth, camHeight, camera.unprojectTable);
		
		


		struct per_sequence
		{
			image_point_cloud renderedPointCloud;
		};

		std::vector<per_sequence> perSequence(calibInput.sequences.size());


		for (uint32 i = 0; i < (uint32)calibInput.sequences.size(); ++i)
		{
			per_sequence& ps = perSequence[i];
			calibration_sequence& s = calibInput.sequences[i];

			ps.renderedPointCloud = projectDepthIntoColorFrame(mesh, s.trackingMat, colorCameraViewMat, colorCameraProjMat, camDistortion, depthToColorTexture, depthBuffer,
				readbackBuffer, colorCameraUnprojectTable);
			ps.renderedPointCloud.erode(5);

			//ps.renderedPointCloud.writeToFile(calibrationBaseDirectory / ("test" + std::to_string(i) + ".ply"));
			//ps.renderedPointCloud.writeToImage(calibrationBaseDirectory / ("test" + std::to_string(i) + ".png"));
			//submitPointCloudForVisualization(ps.renderedPointCloud, vec4(1.f, 0.f, 1.f, 0.f));
		}

		if (cancel)
		{
			cancel = false;
			state = calibration_state_none;
			return;
		}


		quat globalRotation = tracker->globalCameraRotation * tracker->camera.colorSensor.rotation;
		vec3 globalTranslation = tracker->globalCameraRotation * tracker->camera.colorSensor.position + tracker->globalCameraPosition;


		std::unordered_map<std::string, projector_calibration> finalCalibs;

		for (uint32 projID = 0; projID < (uint32)calibInput.projectors.size(); ++projID)
		{
			calibration_projector& proj = calibInput.projectors[projID];

			LOG_MESSAGE("---- Calibrating projector '%ws' ----", proj.uniqueID.c_str());

			uint32 width = proj.width;
			uint32 height = proj.height;

			camera_intrinsics projIntrinsics = startIntrinsics[projID];

			vec3 projPosition;
			quat projRotation;

			
			// Compute initial extrinsics using first sequence.
			{
				calibration_proj_sequence& sequence = proj.sequences[0];
				uint32 globalSequenceID = sequence.sequenceID;

				assert(globalSequenceID < (uint32)perSequence.size());

				per_sequence& ps = perSequence[globalSequenceID];
				auto& renderedPointCloud = ps.renderedPointCloud;

				std::vector<pixel_correspondence> validPixelCorrespondences;
				validPixelCorrespondences.reserve(sequence.allPixelCorrespondences.size());

				for (auto& pc : sequence.allPixelCorrespondences)
				{
					if (renderedPointCloud.validPixelMask((int)pc.camera.y, (int)pc.camera.x) != 0)
					{
						validPixelCorrespondences.push_back(pc);
					}
				}

				std::vector<pixel_correspondence> pixelCorrespondencesSample;
				pixelCorrespondencesSample.reserve(128);
				std::sample(validPixelCorrespondences.begin(), validPixelCorrespondences.end(), std::back_inserter(pixelCorrespondencesSample),
					128, std::mt19937{ std::random_device{}() });

				if (!computeInitialExtrinsicProjectorCalibrationEstimate(pixelCorrespondencesSample, renderedPointCloud, camIntrinsics, camWidth, camHeight, 
					projIntrinsics, width, height, projPosition, projRotation))
				{
					continue;
				}
			}


			if (cancel)
			{
				break;
			}


			// Solve for all projector parameters.
			std::vector<calibration_solver_input> solverInput;

			for (uint32 seqID = 0; seqID < (uint32)proj.sequences.size(); ++seqID)
			{
				calibration_proj_sequence& sequence = proj.sequences[seqID];
				uint32 globalSequenceID = sequence.sequenceID;

				assert(globalSequenceID < (uint32)perSequence.size());

				per_sequence& ps = perSequence[globalSequenceID];
				auto& renderedPointCloud = ps.renderedPointCloud;

				solverInput.emplace_back(renderedPointCloud, sequence.perPixelCorrespondences);
			}

			//solveForCameraToProjectorParameters(solverInput, projPosition, projRotation, projIntrinsics, solverSettings);
			solveForCameraToProjectorParametersUsingCeres(solverInput, projPosition, projRotation, projIntrinsics, solverSettings);

			//submitFrustumForVisualization(projPosition, projRotation, width, height, projIntrinsics, vec4(1.f, 0.f, 1.f, 1.f));

			// Express in global space.
			projRotation = globalRotation * projRotation;
			projPosition = globalRotation * projPosition + globalTranslation;

			if (cancel)
			{
				break;
			}

			finalCalibs[proj.uniqueID] = projector_calibration{ projRotation, projPosition, width, height, projIntrinsics };
		}

		mutex.lock();
		finalCalibrations = std::move(finalCalibs);
		mutex.unlock();

		cancel = false;
		state = calibration_state_none;
	});

	HANDLE handle = (HANDLE)thread.native_handle();
	SetThreadDescription(handle, L"Calibration thread");

	thread.detach();

	return true;
}

void projector_system_calibration::submitPointCloudForVisualization(const image_point_cloud& pc, vec4 color)
{
	struct position_normal
	{
		vec3 position;
		vec3 normal;
	};

	std::vector<position_normal> vertices;
	vertices.reserve(pc.numEntries);

	for (uint32 i = 0; i < pc.entries.width * pc.entries.height; ++i)
	{
		auto& e = pc.entries.data[i];
		if (e.position.z != 0.f)
		{
			vertices.push_back({ e.position, e.normal });
		}
	}

	assert(vertices.size() == pc.numEntries);

	ref<dx_vertex_buffer> vb = createVertexBuffer(sizeof(position_normal), pc.numEntries, vertices.data());

	mutex.lock();
	pointCloudsToVisualize.push_back({ vb, color });
	mutex.unlock();
}

void projector_system_calibration::submitFrustumForVisualization(vec3 position, quat rotation, uint32 width, uint32 height, camera_intrinsics intrinsics, vec4 color)
{
	mutex.lock();
	frustaToVisualize.push_back({ rotation, position, width, height, intrinsics, color });
	mutex.unlock();
}

projector_system_calibration::projector_system_calibration(depth_tracker* tracker, projector_manager* manager)
{
	if (!tracker || !tracker->camera.isInitialized() || !tracker->camera.depthSensor.active || !tracker->camera.colorSensor.active)
	{
		LOG_ERROR("Tracker/camera is not initialized");
		return;
	}

	uint32 width = tracker->camera.colorSensor.width;
	uint32 height = tracker->camera.colorSensor.height;

	for (uint32 i = 0; i < (uint32)win32_window::allConnectedMonitors.size(); ++i)
	{
		auto& monitor = win32_window::allConnectedMonitors[i];
		startIntrinsics[i] = { 3000.f, 3000.f, monitor.width * 0.5f, monitor.height * 0.75f };
	}

	this->tracker = tracker;
	this->manager = manager;
	this->state = calibration_state_none;


	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.renderTargets(DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_D16_UNORM)
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

		depthToColorPipeline = createReloadablePipeline(desc, { "calibration_depth_to_color_vs", "calibration_depth_to_color_ps"});
	}

	{
		static D3D12_INPUT_ELEMENT_DESC inputLayout_position_normal_nonInterleaved[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		auto desc = CREATE_GRAPHICS_PIPELINE
			.inputLayout(inputLayout_position_normal_nonInterleaved)
			.renderTargets(ldrFormat, depthStencilFormat)
			.primitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);

		visualizePointCloudPipeline = createReloadablePipeline(desc, { "calibration_visualize_point_cloud_vs", "calibration_visualize_point_cloud_ps" });
	}

	depthToColorTexture = createTexture(0, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, false, true, false, D3D12_RESOURCE_STATE_GENERIC_READ);
	depthBuffer = createDepthTexture(width, height, DXGI_FORMAT_D16_UNORM);

	uint32 texturePitch = alignTo(depthToColorTexture->width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	readbackBuffer = createReadbackBuffer(sizeof(vec4), texturePitch * height);
}

bool projector_system_calibration::edit(game_scene& scene)
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

	auto& monitors = win32_window::allConnectedMonitors;

	uint32 numProjectors = 0;

	for (uint32 i = 0; i < (uint32)monitors.size(); ++i)
	{
		if (manager->isProjectorIndex[i])
		{
			++numProjectors;

			if (ImGui::BeginTree(monitors[i].uniqueID.c_str()))
			{
				if (ImGui::BeginProperties())
				{
					if (!uiActive)
					{
						ImGui::BeginDisabled();
					}

					ImGui::PropertyInput("Start fx", startIntrinsics[i].fx);
					ImGui::PropertyInput("Start fy", startIntrinsics[i].fy);
					ImGui::PropertyInput("Start cx", startIntrinsics[i].cx);
					ImGui::PropertyInput("Start cy", startIntrinsics[i].cy);

					if (!uiActive)
					{
						ImGui::EndDisabled();
					}

					ImGui::EndProperties();
				}
				ImGui::EndTree();
			}
		}
	}

	if (numProjectors == 0)
	{
		ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1.f), "No projectors selected");
	}


	if (ImGui::BeginProperties())
	{
		if (!uiActive)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PropertySlider("White value", whiteValue);
		ImGui::PropertySlider("Rel. solver correspondence count", solverSettings.percentageOfCorrespondencesToUse);
		ImGui::PropertyDrag("Max num solver iterations", solverSettings.maxNumIterations);

		if (!uiActive)
		{
			ImGui::EndDisabled();
		}


		auto cancelableButton = [numProjectors](const char* label, calibration_state state, calibration_state runningState, volatile bool& cancel)
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
				if (ImGui::PropertyDisableableButton(label, "Go", (numProjectors > 0) && (state == calibration_state_none)))
				{
					return true;
				}
			}

			return false;
		};

		if (cancelableButton("Project calibration patterns", state, calibration_state_projecting_patterns, cancel))
		{
			projectCalibrationPatterns(scene);
		}

		if (cancelableButton("Calibrate", state, calibration_state_calibrating, cancel))
		{
			calibrate(scene);
		}

		ImGui::EndProperties();
	}


	ImGui::Separator();

	ImGui::Text("Disk cache: ./%ws", calibrationBaseDirectory.c_str());
	
	ImGui::SameLine();
	if (ImGui::Button("Open"))
	{
		if (!fs::exists(calibrationBaseDirectory))
		{
			fs::create_directories(calibrationBaseDirectory);
		}
		ShellExecuteW(0, 0, calibrationBaseDirectory.c_str(), 0, 0, SW_SHOWNORMAL);
	}

	ImGui::SameLine();
	if (ImGui::Button("Change"))
	{
		std::string newBaseDir = directoryDialog();
		if (!newBaseDir.empty())
		{
			calibrationBaseDirectory = newBaseDir;
		}
	}

	if (ImGui::DisableableButton("Clear disk cache", uiActive))
	{
		fs::remove_all(calibrationBaseDirectory);
	}

	ImGui::SameLine();

	if (ImGui::DisableableButton("Clear visualizations", uiActive))
	{
		mutex.lock();
		pointCloudsToVisualize.clear();
		frustaToVisualize.clear();
		mutex.unlock();
	}


	return true;
}

void projector_system_calibration::update()
{
	mutex.lock();
	if (finalCalibrations.size() > 0)
	{
		manager->reportLocalCalibration(finalCalibrations);
		finalCalibrations.clear();
	}

	for (software_window* w : windowsToClose)
	{
		delete[] w;
	}
	windowsToClose.clear();
	mutex.unlock();
}


struct visualize_point_cloud_material
{
	vec4 color;
};

struct visualize_point_cloud_pipeline
{
	using material_t = visualize_point_cloud_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(visualize_point_cloud_pipeline)
{
	cl->setPipelineState(*visualizePointCloudPipeline.pipeline);
	cl->setGraphicsRootSignature(*visualizePointCloudPipeline.rootSignature);
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
}

PIPELINE_RENDER_IMPL(visualize_point_cloud_pipeline)
{
	DX_PROFILE_BLOCK(cl, "Render depth camera image");

	visualize_point_cloud_cb cb;
	cb.mvp = viewProj * rc.transform;
	cb.color = rc.material.color;
	
	cl->setGraphics32BitConstants(VISUALIZE_POINT_CLOUD_RS_CB, cb);
	cl->setVertexBuffer(0, rc.vertexBuffer.positions);

	cl->draw(rc.submesh.numVertices, 1, 0, 0);
}


void projector_system_calibration::visualizeIntermediateResults(ldr_render_pass* renderPass)
{
	if (state == calibration_state_uninitialized)
	{
		return;
	}

	quat globalRotation = tracker->globalCameraRotation * tracker->camera.colorSensor.rotation;
	vec3 globalTranslation = tracker->globalCameraRotation * tracker->camera.colorSensor.position + tracker->globalCameraPosition;

	mat4 colorM = createModelMatrix(globalTranslation, globalRotation);

	mutex.lock();
	for (auto& v : pointCloudsToVisualize)
	{
		renderPass->renderObject<visualize_point_cloud_pipeline>(colorM, { v.vertexBuffer }, {}, submesh_info{ 0, 0, 0, v.vertexBuffer->elementCount },
			visualize_point_cloud_material{ v.color });
	}

	for (auto& v : frustaToVisualize)
	{
		render_camera camera;
		camera.initializeCalibrated(globalRotation * v.position + globalTranslation, globalRotation * v.rotation, v.width, v.height, v.intrinsics, 0.1f, 4.f);
		camera.updateMatrices();
		renderCameraFrustum(camera, v.color, renderPass);
	}


	mutex.unlock();
}
