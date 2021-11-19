#include "pch.h"
#include "rgbd_camera.h"

#include <azure-kinect/k4a.h>
#include <realsense/rs.h>
#include <realsense/h/rs_pipeline.h>

static rs2_context* rsContext;
rs2_device_list* deviceList;

std::vector<rgbd_camera_info> rgbd_camera::allConnectedRGBDCameras;

static void checkRealsenseError(rs2_error* e)
{
    if (e)
    {
        std::cerr << "Error was raised when calling " << rs2_get_failed_function(e) << "(" << rs2_get_failed_args(e) << ").\n";
        std::cerr << "    " << rs2_get_error_message(e) << '\n';
        exit(EXIT_FAILURE);
    }
}

void rgbd_camera::initializeCommon()
{
    rs2_error* e = 0;

    rsContext = rs2_create_context(RS2_API_VERSION, &e);
    checkRealsenseError(e);
    deviceList = rs2_query_devices(rsContext, &e);
    checkRealsenseError(e);

    allConnectedRGBDCameras = enumerateRGBDCameras();
}

std::vector<rgbd_camera_info> enumerateRGBDCameras()
{
    std::vector<rgbd_camera_info> result;

    uint32 numAzureDevices = k4a_device_get_installed_count();
    for (uint32 i = 0; i < numAzureDevices; ++i)
    {
        k4a_device_t device;
        k4a_device_open(i, &device);
        if (device)
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
            info.deviceIndex = i;
            info.type = rgbd_camera_type_azure;
            info.serialNumber = serialnum;
            info.description = "Azure camera (" + info.serialNumber + ")";
            result.push_back(info);

            k4a_device_close(device);
        }
    }

    rs2_error* e = 0;
    uint32 numRealsenseDevices = rs2_get_device_count(deviceList, &e);
    checkRealsenseError(e);

    for (uint32 i = 0; i < numRealsenseDevices; ++i)
    {
        rs2_device* dev = rs2_create_device(deviceList, 0, &e);
        checkRealsenseError(e);
        if (dev)
        {
            rgbd_camera_info info;
            info.deviceIndex = i;
            info.type = rgbd_camera_type_realsense;
            info.serialNumber = rs2_get_device_info(dev, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
            checkRealsenseError(e);
            info.description = "Realsense camera (" + info.serialNumber + ")";
            result.push_back(info);

            rs2_delete_device(dev);
        }
    }

    return result;
}

static k4a_color_resolution_t getResolution(camera_resolution res)
{
    switch (res)
    {
        case camera_resolution_off: return K4A_COLOR_RESOLUTION_OFF;
        case camera_resolution_720p: return K4A_COLOR_RESOLUTION_720P;
        case camera_resolution_1080p: return K4A_COLOR_RESOLUTION_1080P;
        case camera_resolution_1440p: return K4A_COLOR_RESOLUTION_1440P;
    }
    assert(false);
    return K4A_COLOR_RESOLUTION_OFF;
}

static uint32 getWidth(camera_resolution res)
{
    switch (res)
    {
        case camera_resolution_off: return 0;
        case camera_resolution_720p: return 1280;
        case camera_resolution_1080p: return 1920;
        case camera_resolution_1440p: return 2560;
    }
    assert(false);
    return 0;
}

static uint32 getHeight(camera_resolution res)
{
    switch (res)
    {
        case camera_resolution_off: return 0;
        case camera_resolution_720p: return 720;
        case camera_resolution_1080p: return 1080;
        case camera_resolution_1440p: return 1440;
    }
    assert(false);
    return 0;
}

void rgbd_camera::operator=(rgbd_camera&& o) noexcept
{
    azure = o.azure;
    o.azure.deviceHandle = 0;

    realsense = o.realsense;
    o.realsense.device = 0;
    
    depthSensor = o.depthSensor;
    colorSensor = o.colorSensor;
    type = o.type;
}

// Use only for depth image.
static void createUnprojectTable(const k4a_calibration_t& calibration, vec2* unprojectTable)
{
    int width = calibration.depth_camera_calibration.resolution_width;
    int height = calibration.depth_camera_calibration.resolution_height;

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
                &calibration, (k4a_float2_t*)&p, 1.f, K4A_CALIBRATION_TYPE_DEPTH, K4A_CALIBRATION_TYPE_DEPTH, (k4a_float3_t*)&ray, &valid);

            ray.y = -ray.y;
            unprojectTable[idx] = valid ? ray.xy : nanv;
        }
    }
}

static void createDefaultUnprojectTable(camera_intrinsics intrinsics, vec2* unprojectTable, uint32 width, uint32 height)
{
    vec2 nanv(nanf(""));

    for (uint32 y = 0, idx = 0; y < height; ++y)
    {
        for (uint32 x = 0; x < width; ++x, ++idx)
        {
            unprojectTable[idx].x = (x - intrinsics.cx) / intrinsics.fx;
            unprojectTable[idx].y = -(y - intrinsics.cy) / intrinsics.fy;
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
    checkRealsenseError(e);

    for (int s = 0; s < numStreams; ++s)
    {
        const rs2_stream_profile* streamProfile = rs2_get_stream_profile(streamList, s, &e);

        rs2_stream stream;
        rs2_format format;
        int index, uniqueID, framerate;
        rs2_get_stream_profile_data(streamProfile, &stream, &format, &index, &uniqueID, &framerate, &e);
        checkRealsenseError(e);

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
    checkRealsenseError(e);

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

bool rgbd_camera::initializeAzure(uint32 deviceIndex, rgbd_camera_spec spec)
{
    k4a_device_configuration_t config = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
    config.camera_fps = K4A_FRAMES_PER_SECOND_30;
    config.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
    config.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
    config.color_resolution = getResolution(spec.colorResolution);
    config.synchronized_images_only = config.color_resolution != K4A_COLOR_RESOLUTION_OFF;

    k4a_device_open(deviceIndex, &azure.deviceHandle);
    if (azure.deviceHandle)
    {
        k4a_device_start_cameras(azure.deviceHandle, &config);

        depthSensor.active = true;
        colorSensor.active = config.color_resolution != K4A_COLOR_RESOLUTION_OFF;

        k4a_calibration_t calibration;
        k4a_device_get_calibration(azure.deviceHandle, config.depth_mode, config.color_resolution, &calibration);

        if (depthSensor.active)
        {
            getCalibration(calibration.depth_camera_calibration, depthSensor);

            depthSensor.unprojectTable = new vec2[depthSensor.width * depthSensor.height];
            createUnprojectTable(calibration, depthSensor.unprojectTable);
        }
        if (colorSensor.active)
        {
            getCalibration(calibration.color_camera_calibration, colorSensor);
        }

        type = rgbd_camera_type_azure;
        depthScale = 0.001f;

        return true;
    }

    return false;
}

bool rgbd_camera::initializeRealsense(uint32 deviceIndex, rgbd_camera_spec spec)
{
    rs2_error* e = 0;

    realsense.device = rs2_create_device(deviceList, 0, &e);
    checkRealsenseError(e);

    if (realsense.device)
    {
        realsense.pipeline = rs2_create_pipeline(rsContext, &e);
        checkRealsenseError(e);
        realsense.config = rs2_create_config(&e);
        checkRealsenseError(e);

        const char* serial = rs2_get_device_info(realsense.device, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
        checkRealsenseError(e);
        rs2_config_enable_device(realsense.config, serial, &e);
        checkRealsenseError(e);

        depthSensor.active = true;
        colorSensor.active = spec.colorResolution != camera_resolution_off;

        if (depthSensor.active)
        {
            rs2_config_enable_stream(realsense.config, RS2_STREAM_DEPTH, -1, 640, 480, RS2_FORMAT_Z16, 30, &e);
            checkRealsenseError(e);
        }
        if (colorSensor.active)
        {
            rs2_config_enable_stream(realsense.config, RS2_STREAM_COLOR, -1, getWidth(spec.colorResolution), getHeight(spec.colorResolution), RS2_FORMAT_BGRA8, 30, &e);
            checkRealsenseError(e);
        }

        realsense.profile = rs2_pipeline_start_with_config(realsense.pipeline, realsense.config, &e);
        checkRealsenseError(e);

        type = rgbd_camera_type_realsense;


        // Get intrinsics and extrinsics.
        rs2_stream_profile_list* profileList = rs2_pipeline_profile_get_streams(realsense.profile, &e);
        checkRealsenseError(e);

        int numProfiles = rs2_get_stream_profiles_count(profileList, &e);
        checkRealsenseError(e);

        const rs2_stream_profile* depthStreamProfile = getStreamProfile(profileList, RS2_STREAM_DEPTH);
        getCalibration(depthStreamProfile, depthStreamProfile, depthSensor);
        
        depthSensor.unprojectTable = new vec2[depthSensor.width * depthSensor.height];
        createDefaultUnprojectTable(depthSensor.intrinsics, depthSensor.unprojectTable, depthSensor.width, depthSensor.height);

        if (colorSensor.active)
        {
            const rs2_stream_profile* colorStreamProfile = getStreamProfile(profileList, RS2_STREAM_COLOR);
            getCalibration(colorStreamProfile, depthStreamProfile, colorSensor);
        }

        rs2_delete_stream_profiles_list(profileList);




        // Get depth scale.
        rs2_sensor_list* sensorList = rs2_query_sensors(realsense.device, &e);
        checkRealsenseError(e);
        int numSensors = rs2_get_sensors_count(sensorList, &e);
        checkRealsenseError(e);

        depthScale = 0.001f;

        for (int i = 0; i < numSensors; ++i)
        {
            rs2_sensor* sensor = rs2_create_sensor(sensorList, i, &e);
            checkRealsenseError(e);

            bool isDepthSensor = rs2_is_sensor_extendable_to(sensor, RS2_EXTENSION_DEPTH_SENSOR, &e);
            checkRealsenseError(e);

            if (isDepthSensor)
            {
                depthScale = rs2_get_depth_scale(sensor, &e);
                //depthScale = rs2_get_option((const rs2_options*)sensor, RS2_OPTION_DEPTH_UNITS, &e);
                break;
            }
        }

        rs2_delete_sensor_list(sensorList);


        return true;
    }

    return false;
}

void rgbd_camera::shutdown()
{
    if (type == rgbd_camera_type_azure && azure.deviceHandle)
    {
        k4a_device_close(azure.deviceHandle);
        azure.deviceHandle = 0;
    }
    else if (type == rgbd_camera_type_realsense && realsense.device)
    {
        rs2_error* e = 0;
        rs2_pipeline_stop(realsense.pipeline, &e);
        checkRealsenseError(e);

        rs2_delete_pipeline_profile(realsense.profile);
        rs2_delete_config(realsense.config);
        rs2_delete_pipeline(realsense.pipeline);
        rs2_delete_device(realsense.device);
        realsense.device = 0;
    }

    if (depthSensor.unprojectTable)
    {
        delete depthSensor.unprojectTable;
        depthSensor.unprojectTable = 0;
    }

    type = rgbd_camera_type_uninitialized;
}

bool rgbd_camera::getFrame(rgbd_frame& result, int32 timeOutInMilliseconds)
{
    if (type == rgbd_camera_type_azure && azure.deviceHandle)
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
    else if (type == rgbd_camera_type_realsense && realsense.device)
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
        checkRealsenseError(e);

        if (newFrames)
        {
            int numFrames = rs2_embedded_frames_count(frames, &e);
            checkRealsenseError(e);

            for (int i = 0; i < numFrames; ++i)
            {
                rs2_frame* frame = rs2_extract_frame(frames, i, &e);
                checkRealsenseError(e);

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
                checkRealsenseError(e);
            }

            rs2_release_frame(frames);

            return true;
        }
    }

    return false;
}

void rgbd_camera::releaseFrame(rgbd_frame& frame)
{
    if (type == rgbd_camera_type_azure)
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
    else if (type == rgbd_camera_type_realsense)
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
