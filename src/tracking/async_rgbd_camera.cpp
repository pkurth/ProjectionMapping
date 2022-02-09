#include "pch.h"
#include "async_rgbd_camera.h"

#include "dx/dx_profiling.h"
#include "dx/dx_command_list.h"
#include "dx/dx_context.h"

void async_rgbd_camera::shutdown()
{
    if (colorFrame)
    {
        delete[] colorFrame;
        colorFrame = 0;
    }
}

bool async_rgbd_camera::initializeAs(rgbd_camera_type type, uint32 deviceIndex, bool alignDepthToColor)
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

bool async_rgbd_camera::initializeAzure(uint32 deviceIndex, bool alignDepthToColor)
{
    if (!rgbd_camera::initializeAzure(deviceIndex, alignDepthToColor))
    {
        return false;
    }

    initializeInternal();

	return true;
}

bool async_rgbd_camera::initializeRealsense(uint32 deviceIndex, bool alignDepthToColor)
{
    if (!rgbd_camera::initializeRealsense(deviceIndex, alignDepthToColor))
    {
        return false;
    }

    initializeInternal();

    return true;
}

void async_rgbd_camera::initializeInternal()
{
    colorFrame = new color_bgra[colorSensor.width * colorSensor.height];

    depthTexture = createTexture(0, depthSensor.width, depthSensor.height, trackingDepthFormat, false, false, false, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint32 requiredSize = (uint32)GetRequiredIntermediateSize(depthTexture->resource.Get(), 0, 1);
    depthUploadBuffer = createUploadBuffer(requiredSize, 1, 0);

    if (colorSensor.active)
    {
        colorTexture = createTexture(0, colorSensor.width, colorSensor.height, trackingColorFormat, false, false, false, D3D12_RESOURCE_STATE_GENERIC_READ);
        uint32 requiredSize = (uint32)GetRequiredIntermediateSize(colorTexture->resource.Get(), 0, 1);
        colorUploadBuffer = createUploadBuffer(requiredSize, 1, 0);
    }

    depthUnprojectTableTexture = createTexture(depthSensor.unprojectTable, depthSensor.width, depthSensor.height, DXGI_FORMAT_R32G32_FLOAT);

    SET_NAME(depthTexture->resource, "Camera depth");
    SET_NAME(depthUnprojectTableTexture->resource, "Unproject table");


    pollingThread = std::thread([this]()
    {
        // Upload new frame if available.
        rgbd_frame frame;
        if (getFrame(frame, 0))
        {
            dx_command_list* cl = dxContext.getFreeRenderCommandList();

            if (depthSensor.active)
            {
                PROFILE_ALL(cl, "Upload depth image");

                uint32 formatSize = getFormatSize(trackingDepthFormat);

                D3D12_SUBRESOURCE_DATA subresource;
                subresource.RowPitch = depthSensor.width * formatSize;
                subresource.SlicePitch = depthSensor.width * depthSensor.height * formatSize;
                subresource.pData = frame.depth;

                cl->transitionBarrier(depthTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
                UpdateSubresources<1>(cl->commandList.Get(), depthTexture->resource.Get(), depthUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
                cl->transitionBarrier(depthTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
            }

            if (colorSensor.active)
            {
                PROFILE_ALL(cl, "Upload color image");

                uint32 formatSize = getFormatSize(trackingColorFormat);

                D3D12_SUBRESOURCE_DATA subresource;
                subresource.RowPitch = colorSensor.width * formatSize;
                subresource.SlicePitch = colorSensor.width * colorSensor.height * formatSize;
                subresource.pData = frame.color;

                cl->transitionBarrier(colorTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
                UpdateSubresources<1>(cl->commandList.Get(), colorTexture->resource.Get(), colorUploadBuffer->resource.Get(), 0, 0, 1, &subresource);
                cl->transitionBarrier(colorTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

                memcpy(colorFrame, frame.color, colorSensor.width * colorSensor.height * sizeof(color_bgra));
            }

            releaseFrame(frame);

            dxContext.executeCommandList(cl);
        }
    });

}
