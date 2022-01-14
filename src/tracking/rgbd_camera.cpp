#include "pch.h"
#include "rgbd_camera.h"

#include <azure-kinect/k4a.h>
#include <realsense/rs.h>
#include <realsense/h/rs_pipeline.h>

static rs2_context* rsContext;

std::vector<rgbd_camera_info> rgbd_camera::allConnectedRGBDCameras;

static void printRealsenseError(rs2_error* e)
{
    std::cerr << "Realsense error in function " << rs2_get_failed_function(e) << "(" << rs2_get_failed_args(e) << ").\n";
    std::cerr << "    " << rs2_get_error_message(e) << '\n';
}

static void assertRealsenseSuccess(rs2_error* e)
{
    if (e)
    {
        printRealsenseError(e);
        exit(EXIT_FAILURE);
    }
}

static bool checkRealsenseSuccess(rs2_error* e)
{
    if (e)
    {
        printRealsenseError(e);
        return false;
    }
    return true;
}

void rgbd_camera::initializeCommon()
{
    rs2_error* e = 0;

    rsContext = rs2_create_context(RS2_API_VERSION, &e);
    assertRealsenseSuccess(e);

    enumerate();
}

static rgbd_camera_info getInfo(k4a_device_t device, uint32 deviceIndex)
{
    std::string serialnum = "";
    size_t bufferSize = 0;

    if (k4a_device_get_serialnum(device, &serialnum[0], &bufferSize) == K4A_BUFFER_RESULT_TOO_SMALL && bufferSize > 1)
    {
        serialnum.resize(bufferSize);
        if (k4a_device_get_serialnum(device, &serialnum[0], &bufferSize) == K4A_BUFFER_RESULT_SUCCEEDED && serialnum[bufferSize - 1] == 0)
        {
            serialnum.resize(bufferSize - 1);
        }
    }

    rgbd_camera_info info;
    info.deviceIndex = deviceIndex;
    info.type = rgbd_camera_type_azure;
    info.serialNumber = serialnum;
    info.description = "Azure camera (" + info.serialNumber + ")";

    return info;
}

static rgbd_camera_info getInfo(rs2_device* dev, uint32 deviceIndex)
{
    rs2_error* e = 0;

    rgbd_camera_info info;
    info.deviceIndex = deviceIndex;
    info.type = rgbd_camera_type_realsense;
    info.serialNumber = rs2_get_device_info(dev, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
    assertRealsenseSuccess(e);
    info.description = "Realsense camera (" + info.serialNumber + ")";

    return info;
}

std::vector<rgbd_camera_info>& rgbd_camera::enumerate()
{
    allConnectedRGBDCameras.clear();

    uint32 numAzureDevices = k4a_device_get_installed_count();
    for (uint32 i = 0; i < numAzureDevices; ++i)
    {
        k4a_device_t device;
        k4a_device_open(i, &device);
        if (device)
        {
            rgbd_camera_info info = getInfo(device, i);
            allConnectedRGBDCameras.push_back(info);

            k4a_device_close(device);
        }
    }

    rs2_error* e = 0;

    rs2_device_list* deviceList = rs2_query_devices(rsContext, &e);
    assertRealsenseSuccess(e);

    uint32 numRealsenseDevices = rs2_get_device_count(deviceList, &e);
    assertRealsenseSuccess(e);

    for (uint32 i = 0; i < numRealsenseDevices; ++i)
    {
        rs2_device* dev = rs2_create_device(deviceList, 0, &e);
        assertRealsenseSuccess(e);
        if (dev)
        {
            rgbd_camera_info info = getInfo(dev, i);
            allConnectedRGBDCameras.push_back(info);

            rs2_delete_device(dev);
        }
    }

    return allConnectedRGBDCameras;
}

void rgbd_camera::operator=(rgbd_camera&& o) noexcept
{
    azure = o.azure;
    o.azure = {};

    realsense = o.realsense;
    o.realsense = {};

    alignDepthToColor = o.alignDepthToColor;
    
    depthSensor = o.depthSensor;
    colorSensor = o.colorSensor;
    info = std::move(o.info);

    o.depthSensor.unprojectTable = 0;
    o.colorSensor.unprojectTable = 0;
}

static void createUnprojectTable(const k4a_calibration_t& calibration, vec2* unprojectTable, bool depth)
{
    int width = depth ? calibration.depth_camera_calibration.resolution_width : calibration.color_camera_calibration.resolution_width;
    int height = depth ? calibration.depth_camera_calibration.resolution_height : calibration.color_camera_calibration.resolution_height;
    k4a_calibration_type_t type = depth ? K4A_CALIBRATION_TYPE_DEPTH : K4A_CALIBRATION_TYPE_COLOR;

    vec2 p;
    vec2 nanv(nanf(""));

    for (int y = 0, idx = 0; y < height; ++y)
    {
        p.y = (float)y;
        for (int x = 0; x < width; ++x, ++idx)
        {
            p.x = (float)x;

            vec3 ray;
            int valid;
            k4a_calibration_2d_to_3d(
                &calibration, (k4a_float2_t*)&p, 1.f, type, type, (k4a_float3_t*)&ray, &valid);

            ray.y = -ray.y;
            unprojectTable[idx] = valid ? ray.xy : nanv;
        }
    }
}

static void createUnprojectTable(const rs2_stream_profile* streamProfile, vec2* unprojectTable)
{
    rs2_error* e = 0;

    rs2_intrinsics intrinsics;
    rs2_get_video_stream_intrinsics(streamProfile, &intrinsics, &e);
    assertRealsenseSuccess(e);

    vec2 p;

    for (int y = 0, idx = 0; y < intrinsics.height; ++y)
    {
        p.y = (float)y;
        for (int x = 0; x < intrinsics.width; ++x, ++idx)
        {
            p.x = (float)x;

            vec3 ray;
            rs2_deproject_pixel_to_point(ray.data, &intrinsics, p.data, 1.f);

            ray.y = -ray.y;
            unprojectTable[idx] = ray.xy;
        }
    }
}

static void getCalibration(const k4a_calibration_camera_t& calib, rgbd_camera_sensor& result)
{
    result.width = calib.resolution_width;
    result.height = calib.resolution_height;

    result.intrinsics.fx = calib.intrinsics.parameters.param.fx;
    result.intrinsics.fy = calib.intrinsics.parameters.param.fy;
    result.intrinsics.cx = calib.intrinsics.parameters.param.cx;
    result.intrinsics.cy = calib.intrinsics.parameters.param.cy;

    result.distortion.k1 = calib.intrinsics.parameters.param.k1;
    result.distortion.k2 = calib.intrinsics.parameters.param.k2;
    result.distortion.k3 = calib.intrinsics.parameters.param.k3;
    result.distortion.k4 = calib.intrinsics.parameters.param.k4;
    result.distortion.k5 = calib.intrinsics.parameters.param.k5;
    result.distortion.k6 = calib.intrinsics.parameters.param.k6;
    result.distortion.p1 = calib.intrinsics.parameters.param.p1;
    result.distortion.p2 = calib.intrinsics.parameters.param.p2;

    mat3 R;
    memcpy(R.m, calib.extrinsics.rotation, sizeof(float) * 9);  // Matrix is row-major.

#if !ROW_MAJOR
    R = transpose(R);
#endif

    R.m10 *= -1.f;
    R.m20 *= -1.f;
    R.m01 *= -1.f;
    R.m02 *= -1.f;
    quat rotation = mat3ToQuaternion(R);

    vec3 position;
    memcpy(position.data, calib.extrinsics.translation, sizeof(float) * 3);
    position.y *= -1.f;
    position.z *= -1.f;

    position *= 1.f / 1000.f;

    result.rotation = conjugate(rotation);
    result.position = -(conjugate(rotation) * position);
}

static const rs2_stream_profile* getStreamProfile(rs2_stream_profile_list* streamList, rs2_stream streamType)
{
    rs2_error* e = 0;

    int numStreams = rs2_get_stream_profiles_count(streamList, &e);
    assertRealsenseSuccess(e);

    for (int s = 0; s < numStreams; ++s)
    {
        const rs2_stream_profile* streamProfile = rs2_get_stream_profile(streamList, s, &e);

        rs2_stream stream;
        rs2_format format;
        int index, uniqueID, framerate;
        rs2_get_stream_profile_data(streamProfile, &stream, &format, &index, &uniqueID, &framerate, &e);
        assertRealsenseSuccess(e);

        if (stream == streamType)
        {
            return streamProfile;
        }
    }

    return 0;
}

static void getCalibration(const rs2_stream_profile* streamProfile, const rs2_stream_profile* depthStreamProfile, rgbd_camera_sensor& result)
{
    rs2_error* e = 0;

    rs2_intrinsics intrinsics;
    rs2_get_video_stream_intrinsics(streamProfile, &intrinsics, &e);
    assertRealsenseSuccess(e);

    result.width = intrinsics.width;
    result.height = intrinsics.height;

    result.intrinsics.fx = intrinsics.fx;
    result.intrinsics.fy = intrinsics.fy;
    result.intrinsics.cx = intrinsics.ppx;
    result.intrinsics.cy = intrinsics.ppy;

    result.distortion.k1 = intrinsics.coeffs[0];
    result.distortion.k2 = intrinsics.coeffs[1];
    result.distortion.k3 = intrinsics.coeffs[4];
    result.distortion.k4 = 0.f;
    result.distortion.k5 = 0.f;
    result.distortion.k6 = 0.f;
    result.distortion.p1 = intrinsics.coeffs[2];
    result.distortion.p2 = intrinsics.coeffs[3];

    rs2_extrinsics extrinsics;
    rs2_get_extrinsics(streamProfile, depthStreamProfile, &extrinsics, &e);

    mat3 R;
    memcpy(R.m, extrinsics.rotation, sizeof(float) * 9); // Matrix is column-major.

#if ROW_MAJOR
    R = transpose(R);
#endif

    R.m10 *= -1.f;
    R.m20 *= -1.f;
    R.m01 *= -1.f;
    R.m02 *= -1.f;
    quat rotation = mat3ToQuaternion(R);

    vec3 position;
    memcpy(position.data, extrinsics.translation, sizeof(float) * 3);
    position.y *= -1.f;
    position.z *= -1.f;

    result.rotation = rotation;
    result.position = position;
}

bool rgbd_camera::initializeAzure(uint32 deviceIndex, bool alignDepthToColor)
{
    shutdown();

    k4a_device_configuration_t config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
    config.camera_fps = K4A_FRAMES_PER_SECOND_30;
    config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
    config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
    config.color_resolution = K4A_COLOR_RESOLUTION_720P;
    config.synchronized_images_only = config.color_resolution != K4A_COLOR_RESOLUTION_OFF;

    this->alignDepthToColor = alignDepthToColor;

    k4a_device_open(deviceIndex, &azure.deviceHandle);
    if (azure.deviceHandle)
    {
        k4a_device_start_cameras(azure.deviceHandle, &config);

        depthSensor.active = true;
        colorSensor.active = true;

        k4a_calibration_t calibration;
        k4a_device_get_calibration(azure.deviceHandle, config.depth_mode, config.color_resolution, &calibration);

        if (depthSensor.active)
        {
            getCalibration(calibration.depth_camera_calibration, depthSensor);

            depthSensor.unprojectTable = new vec2[depthSensor.width * depthSensor.height];
            createUnprojectTable(calibration, depthSensor.unprojectTable, true);
        }
        if (colorSensor.active)
        {
            getCalibration(calibration.color_camera_calibration, colorSensor);

            colorSensor.unprojectTable = new vec2[colorSensor.width * colorSensor.height];
            createUnprojectTable(calibration, colorSensor.unprojectTable, false);
        }

        depthScale = 0.001f;

        info = getInfo(azure.deviceHandle, deviceIndex);

        return true;
    }

    return false;
}

bool rgbd_camera::initializeRealsense(uint32 deviceIndex, bool alignDepthToColor)
{
    shutdown();

    this->alignDepthToColor = alignDepthToColor;

    rs2_error* e = 0;

    rs2_device_list* deviceList = rs2_query_devices(rsContext, &e);
    realsense.device = rs2_create_device(deviceList, deviceIndex, &e);
    if (!checkRealsenseSuccess(e))
    {
        return false;
    }

    if (realsense.device)
    {
        realsense.pipeline = rs2_create_pipeline(rsContext, &e);
        assertRealsenseSuccess(e);
        realsense.config = rs2_create_config(&e);
        assertRealsenseSuccess(e);

        const char* serial = rs2_get_device_info(realsense.device, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
        assertRealsenseSuccess(e);
        rs2_config_enable_device(realsense.config, serial, &e);
        assertRealsenseSuccess(e);

        depthSensor.active = true;
        colorSensor.active = true;

        uint32 colorWidth = 1280;
        uint32 colorHeight = 720;

        uint32 depthWidth = 640;
        uint32 depthHeight = 480;

        if (colorSensor.active)
        {
            rs2_config_enable_stream(realsense.config, RS2_STREAM_COLOR, -1, colorWidth, colorHeight, RS2_FORMAT_BGRA8, 30, &e);
            assertRealsenseSuccess(e);
        }
        if (depthSensor.active)
        {
            rs2_config_enable_stream(realsense.config, RS2_STREAM_DEPTH, -1, depthWidth, depthHeight, RS2_FORMAT_Z16, 30, &e);
            assertRealsenseSuccess(e);
        }

        realsense.profile = rs2_pipeline_start_with_config(realsense.pipeline, realsense.config, &e);
        assertRealsenseSuccess(e);



        // Get intrinsics and extrinsics.
        rs2_stream_profile_list* profileList = rs2_pipeline_profile_get_streams(realsense.profile, &e);
        assertRealsenseSuccess(e);

        int numProfiles = rs2_get_stream_profiles_count(profileList, &e);
        assertRealsenseSuccess(e);

        const rs2_stream_profile* depthStreamProfile = getStreamProfile(profileList, RS2_STREAM_DEPTH);
        getCalibration(depthStreamProfile, depthStreamProfile, depthSensor);
        
        depthSensor.unprojectTable = new vec2[depthSensor.width * depthSensor.height];
        createUnprojectTable(depthStreamProfile, depthSensor.unprojectTable);

        if (colorSensor.active)
        {
            const rs2_stream_profile* colorStreamProfile = getStreamProfile(profileList, RS2_STREAM_COLOR);
            getCalibration(colorStreamProfile, depthStreamProfile, colorSensor);

            colorSensor.unprojectTable = new vec2[colorSensor.width * colorSensor.height];
            createUnprojectTable(colorStreamProfile, colorSensor.unprojectTable);

            if (alignDepthToColor)
            {
                delete[] depthSensor.unprojectTable;

                colorSensor.position = vec3(0.f, 0.f, 0.f);
                colorSensor.rotation = quat::identity;

                depthSensor = colorSensor;
            }
        }

        rs2_delete_stream_profiles_list(profileList);



        // Get depth scale.
        rs2_sensor_list* sensorList = rs2_query_sensors(realsense.device, &e);
        assertRealsenseSuccess(e);
        int numSensors = rs2_get_sensors_count(sensorList, &e);
        assertRealsenseSuccess(e);

        depthScale = 0.001f;

        for (int i = 0; i < numSensors; ++i)
        {
            rs2_sensor* sensor = rs2_create_sensor(sensorList, i, &e);
            assertRealsenseSuccess(e);

            bool isDepthSensor = rs2_is_sensor_extendable_to(sensor, RS2_EXTENSION_DEPTH_SENSOR, &e);
            assertRealsenseSuccess(e);

            if (isDepthSensor)
            {
                depthScale = rs2_get_depth_scale(sensor, &e);
                assertRealsenseSuccess(e);
                //depthScale = rs2_get_option((const rs2_options*)sensor, RS2_OPTION_DEPTH_UNITS, &e);
                break;
            }
        }

        rs2_delete_sensor_list(sensorList);

        info = getInfo(realsense.device, deviceIndex);



        if (alignDepthToColor)
        {
            realsense.align = rs2_create_align(RS2_STREAM_COLOR, &e);
            assertRealsenseSuccess(e);
            realsense.alignQueue = rs2_create_frame_queue(1, &e);
            assertRealsenseSuccess(e);
            rs2_start_processing_queue(realsense.align, realsense.alignQueue, &e);
            assertRealsenseSuccess(e);
        }

        return true;
    }

    return false;
}

bool rgbd_camera::initializeAs(rgbd_camera_type type, uint32 deviceIndex, bool alignDepthToColor)
{
    switch (type)
    {
        case rgbd_camera_type_azure:
            return initializeAzure(deviceIndex, alignDepthToColor);
        case rgbd_camera_type_realsense:
            return initializeRealsense(deviceIndex, alignDepthToColor);
        default:
            return false;
    }
}

void rgbd_camera::shutdown()
{
    if (info.type == rgbd_camera_type_azure && azure.deviceHandle)
    {
        k4a_device_close(azure.deviceHandle);
        azure.deviceHandle = 0;
    }
    else if (info.type == rgbd_camera_type_realsense && realsense.device)
    {
        rs2_error* e = 0;
        rs2_pipeline_stop(realsense.pipeline, &e);
        assertRealsenseSuccess(e);

        rs2_delete_pipeline_profile(realsense.profile);
        rs2_delete_config(realsense.config);
        rs2_delete_pipeline(realsense.pipeline);
        rs2_delete_device(realsense.device);
        if (alignDepthToColor)
        {
            rs2_delete_processing_block(realsense.align);
            rs2_delete_frame_queue(realsense.alignQueue);
        }
        realsense.device = 0;
    }

    if (depthSensor.unprojectTable)
    {
        delete depthSensor.unprojectTable;
    }

    if (!alignDepthToColor && colorSensor.unprojectTable)
    {
        delete colorSensor.unprojectTable;
    }

    depthSensor.unprojectTable = 0;
    colorSensor.unprojectTable = 0;

    info.type = rgbd_camera_type_uninitialized;
}

bool rgbd_camera::getFrame(rgbd_frame& result, int32 timeOutInMilliseconds)
{
    if (info.type == rgbd_camera_type_azure && azure.deviceHandle)
    {
        k4a_capture_t captureHandle;

        if (k4a_device_get_capture(azure.deviceHandle, &captureHandle, timeOutInMilliseconds) == K4A_WAIT_RESULT_SUCCEEDED)
        {
            if (depthSensor.active)
            {
                result.azureDepthHandle = k4a_capture_get_depth_image(captureHandle);
                result.depth = (uint16*)k4a_image_get_buffer(result.azureDepthHandle);
            }

            if (colorSensor.active)
            {
                result.azureColorHandle = k4a_capture_get_color_image(captureHandle);
                result.color = (color_bgra*)k4a_image_get_buffer(result.azureColorHandle);
            }

            k4a_capture_release(captureHandle);

            return true;
        }
    }
    else if (info.type == rgbd_camera_type_realsense && realsense.device)
    {
        rs2_frame* frames = 0;
        rs2_error* e = 0;
        bool newFrames = false;

        if (timeOutInMilliseconds == 0)
        {
            newFrames = rs2_pipeline_poll_for_frames(realsense.pipeline, &frames, &e);
        }
        else
        {
            newFrames = rs2_pipeline_try_wait_for_frames(realsense.pipeline, &frames, timeOutInMilliseconds, &e);
        }
        assertRealsenseSuccess(e);

        if (newFrames)
        {
            if (alignDepthToColor)
            {
                rs2_process_frame(realsense.align, frames, &e);
                assertRealsenseSuccess(e);
                int numAlignedFrames = rs2_poll_for_frame(realsense.alignQueue, &frames, &e);
                assert(numAlignedFrames == 1);
            }


            int numFrames = rs2_embedded_frames_count(frames, &e);
            assertRealsenseSuccess(e);

            for (int i = 0; i < numFrames; ++i)
            {
                rs2_frame* frame = rs2_extract_frame(frames, i, &e);
                assertRealsenseSuccess(e);

                if (rs2_is_frame_extendable_to(frame, RS2_EXTENSION_DEPTH_FRAME, &e))
                {
                    result.realsenseDepthHandle = frame;
                    result.depth = (uint16*)rs2_get_frame_data(frame, &e);
                }
                else
                {
                    result.realsenseColorHandle = frame;
                    result.color = (color_bgra*)rs2_get_frame_data(frame, &e);
                }
                assertRealsenseSuccess(e);
            }

            rs2_release_frame(frames);

            return true;
        }
    }

    return false;
}

void rgbd_camera::releaseFrame(rgbd_frame& frame)
{
    if (info.type == rgbd_camera_type_azure)
    {
        if (frame.azureDepthHandle)
        {
            k4a_image_release(frame.azureDepthHandle);
            frame.azureDepthHandle = 0;
        }
        if (frame.azureColorHandle)
        {
            k4a_image_release(frame.azureColorHandle);
            frame.azureColorHandle = 0;
        }
    }
    else if (info.type == rgbd_camera_type_realsense)
    {
        if (frame.realsenseDepthHandle)
        {
            rs2_release_frame(frame.realsenseDepthHandle);
            frame.realsenseDepthHandle = 0;
        }
        if (frame.realsenseColorHandle)
        {
            rs2_release_frame(frame.realsenseColorHandle);
            frame.realsenseColorHandle = 0;
        }
    }

    frame.depth = 0;
    frame.color = 0;
}
