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
	rgbd_camera_type type = rgbd_camera_type_uninitialized;
	std::string serialNumber;
	std::string description;
};

struct color_bgra
{
	uint8 b, g, r, a;
};

struct rgbd_frame
{
	uint16* depth = 0;
	color_bgra* color = 0;

private:
	struct _k4a_image_t* azureDepthHandle = 0;
	struct _k4a_image_t* azureColorHandle = 0;

	struct rs2_frame* realsenseDepthHandle = 0;
	struct rs2_frame* realsenseColorHandle = 0;

	friend struct rgbd_camera;
};

struct rgbd_camera_sensor
{
	bool active;
	uint32 width;
	uint32 height;

	quat rotation;
	vec3 position;

	camera_intrinsics intrinsics;
	camera_distortion distortion;

	vec2* unprojectTable = 0; // Maps pixel to ray direction (x, y). z = -1.
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
			// Process frame here (copy to texture etc).

			cam.releaseFrame(frame);
		}
	}
*/

struct azure_handle
{
	struct _k4a_device_t* deviceHandle = 0;

	struct _k4a_transformation_t* alignTransform;
};

struct realsense_handle
{
	struct rs2_device* device = 0;
	struct rs2_pipeline* pipeline = 0;
	struct rs2_config* config = 0;
	struct rs2_pipeline_profile* profile = 0;

	struct rs2_processing_block* align = 0;
	struct rs2_frame_queue* alignQueue = 0;
};

struct rgbd_camera
{
	rgbd_camera() = default;
	rgbd_camera(const rgbd_camera&) = delete;
	rgbd_camera(rgbd_camera&& o) noexcept{ *this = std::move(o); }
	~rgbd_camera() { shutdown(); }

	void operator=(const rgbd_camera&) = delete;
	void operator=(rgbd_camera&& o) noexcept;

	bool initializeAs(rgbd_camera_type type, uint32 deviceIndex = 0, bool alignDepthToColor = true);
	bool initializeAzure(uint32 deviceIndex = 0, bool alignDepthToColor = true);
	bool initializeRealsense(uint32 deviceIndex = 0, bool alignDepthToColor = true);
	void shutdown();

	bool getFrame(rgbd_frame& result, int32 timeOutInMilliseconds = 0); // 0: Return immediately.
	void releaseFrame(rgbd_frame& frame);

	azure_handle azure;
	realsense_handle realsense;

	rgbd_camera_info info;

	rgbd_camera_sensor colorSensor;
	rgbd_camera_sensor depthSensor;

	float depthScale;
	bool alignDepthToColor;

	static void initializeCommon();
	static std::vector<rgbd_camera_info>& enumerate();

	static std::vector<rgbd_camera_info> allConnectedRGBDCameras;
};
