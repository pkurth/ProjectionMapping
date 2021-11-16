#include "core/math.h"
#include "core/camera.h"

enum rgbd_camera_type
{
	rgbd_camera_type_uninitialized,
	rgbd_camera_type_azure,
	rgbd_camera_type_realsense,
};

static const char* rgbdCameraTypeNames[] =
{
	"Uninitialized",
	"Azure",
	"Realsense",
};

struct rgbd_camera_info
{
	uint32 deviceIndex;
	rgbd_camera_type type;
	std::string serialNumber;
	std::string description;
};

std::vector<rgbd_camera_info> enumerateRGBDCameras();

enum camera_resolution
{
	camera_resolution_off,
	camera_resolution_720p,
	camera_resolution_1080p,
	camera_resolution_1440p,
};

struct rgbd_camera_spec
{
	camera_resolution colorResolution = camera_resolution_720p;
};

struct color_bgra
{
	uint8 b, g, r, a;
};

struct rgbd_frame
{
	uint16* depth = 0;
	color_bgra* color = 0;


	// Internal. Do not use.
	struct _k4a_image_t* azureDepthHandle = 0;
	struct _k4a_image_t* azureColorHandle = 0;

	struct rs2_frame* realsenseDepthHandle = 0;
	struct rs2_frame* realsenseColorHandle = 0;
};

struct rgbd_camera_sensor
{
	bool active;
	uint32 width;
	uint32 height;

	quat rotation;
	vec3 position;

	camera_intrinsics intrinsics;

	vec2* xyTable = 0; // Maps pixel to ray direction (x, y). z = -1.
};


/*
	Usage:

	rgbd_camera cam;
	cam.initialize(0);

	while (running)
	{
		rgbd_frame frame;
		if (cam.getFrame(frame, 0))
		{
			// Process frame here.

			cam.releaseFrame(frame);
		}
	}
*/

struct azure_handle
{
	struct _k4a_device_t* deviceHandle = 0;
};

struct realsense_handle
{
	struct rs2_device* device = 0;
	struct rs2_pipeline* pipeline = 0;
	struct rs2_config* config = 0;
	struct rs2_pipeline_profile* profile = 0;
};

struct rgbd_camera
{
	rgbd_camera() = default;
	rgbd_camera(const rgbd_camera&) = delete;
	rgbd_camera(rgbd_camera&& o) noexcept{ *this = std::move(o); }
	~rgbd_camera() { shutdown(); }

	void operator=(const rgbd_camera&) = delete;
	void operator=(rgbd_camera&& o) noexcept;

	bool initializeAzure(uint32 deviceIndex = 0, rgbd_camera_spec spec = {});
	bool initializeRealsense(uint32 deviceIndex = 0, rgbd_camera_spec spec = {});
	void shutdown();

	bool getFrame(rgbd_frame& result, int32 timeOutInMilliseconds = 0); // 0: Return immediately.
	void releaseFrame(rgbd_frame& frame);

	rgbd_camera_type type = rgbd_camera_type_uninitialized;
	azure_handle azure;
	realsense_handle realsense;

	rgbd_camera_sensor colorSensor;
	rgbd_camera_sensor depthSensor;

	float depthScale;


	static void initializeCommon();
	static std::vector<rgbd_camera_info> allConnectedRGBDCameras;
};
