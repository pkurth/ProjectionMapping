#include "pch.h"
#include "tracking.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"

static const DXGI_FORMAT trackingDepthFormat = DXGI_FORMAT_R16_UINT;

depth_tracker::depth_tracker()
{
	if (!camera.initializeRealsense())
	{
		return;
	}

	cameraDepthTexture = createTexture(0, camera.depthSensor.width, camera.depthSensor.height, trackingDepthFormat);
	uint32 requiredSize = (uint32)GetRequiredIntermediateSize(cameraDepthTexture->resource.Get(), 0, 1);
	uploadBuffer = createUploadBuffer(requiredSize, 1, 0);
}

void depth_tracker::update()
{
	rgbd_frame frame;
	if (camera.getFrame(frame, 0))
	{
		uint32 formatSize = getFormatSize(trackingDepthFormat);

		D3D12_SUBRESOURCE_DATA subresource;
		subresource.RowPitch = camera.depthSensor.width * formatSize;
		subresource.SlicePitch = camera.depthSensor.width * camera.depthSensor.height * formatSize;
		subresource.pData = frame.depth;

		dx_command_list* cl = dxContext.getFreeCopyCommandList();
		cl->transitionBarrier(cameraDepthTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		UpdateSubresources<1>(cl->commandList.Get(), cameraDepthTexture->resource.Get(), uploadBuffer->resource.Get(), 0, 0, 1, &subresource);

		dxContext.executeCommandList(cl);

		camera.releaseFrame(frame);
	}
}
