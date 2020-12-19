#include "pch.h"
#include "dx_texture.h"
#include "dx_context.h"
#include "texture_preprocessing.h"
#include "dx_command_list.h"

#include <DirectXTex/DirectXTex.h>

#include <filesystem>

namespace fs = std::filesystem;



static DXGI_FORMAT makeSRGB(DXGI_FORMAT format)
{
	return DirectX::MakeSRGB(format);
}

static DXGI_FORMAT makeLinear(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		format = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;

	case DXGI_FORMAT_BC1_UNORM_SRGB:
		format = DXGI_FORMAT_BC1_UNORM;
		break;

	case DXGI_FORMAT_BC2_UNORM_SRGB:
		format = DXGI_FORMAT_BC2_UNORM;
		break;

	case DXGI_FORMAT_BC3_UNORM_SRGB:
		format = DXGI_FORMAT_BC3_UNORM;
		break;

	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		format = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;

	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		format = DXGI_FORMAT_B8G8R8X8_UNORM;
		break;

	case DXGI_FORMAT_BC7_UNORM_SRGB:
		format = DXGI_FORMAT_BC7_UNORM;
		break;
	}
	return format;
}

static bool loadImageFromFile(const char* filepathRaw, uint32 flags, DirectX::ScratchImage& scratchImage, D3D12_RESOURCE_DESC& textureDesc)
{
	if (flags & texture_load_flags_gen_mips_on_gpu)
	{
		flags &= ~texture_load_flags_gen_mips_on_cpu;
		flags |= texture_load_flags_allocate_full_mipchain;
	}


	fs::path filepath = filepathRaw;
	fs::path extension = filepath.extension();

	fs::path cachedFilename = filepath;
	cachedFilename.replace_extension("." + std::to_string(flags) + ".cache.dds");

	fs::path cacheFilepath = L"bin_cache" / cachedFilename;

	bool fromCache = false;
	DirectX::TexMetadata metadata;

	if (!(flags & texture_load_flags_always_load_from_source))
	{
		// Look for cached.

		WIN32_FILE_ATTRIBUTE_DATA cachedData;
		if (GetFileAttributesExW(cacheFilepath.c_str(), GetFileExInfoStandard, &cachedData))
		{
			FILETIME cachedFiletime = cachedData.ftLastWriteTime;

			WIN32_FILE_ATTRIBUTE_DATA originalData;
			assert(GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &originalData));
			FILETIME originalFiletime = originalData.ftLastWriteTime;

			if (CompareFileTime(&cachedFiletime, &originalFiletime) >= 0)
			{
				// Cached file is newer than original, so load this.
				fromCache = SUCCEEDED(DirectX::LoadFromDDSFile(cacheFilepath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage));
			}
		}
	}

	if (!fromCache)
	{
		if (extension == ".dds")
		{
			if (FAILED(DirectX::LoadFromDDSFile(filepath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage)))
			{
				return false;
			}
		}
		else if (extension == ".hdr")
		{
			if (FAILED(DirectX::LoadFromHDRFile(filepath.c_str(), &metadata, scratchImage)))
			{
				return false;
			}
		}
		else if (extension == ".tga")
		{
			if (FAILED(DirectX::LoadFromTGAFile(filepath.c_str(), &metadata, scratchImage)))
			{
				return false;
			}
		}
		else
		{
			if (FAILED(DirectX::LoadFromWICFile(filepath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, scratchImage)))
			{
				return false;
			}
		}

		if (flags & texture_load_flags_noncolor)
		{
			metadata.format = makeLinear(metadata.format);
		}
		else
		{
			metadata.format = makeSRGB(metadata.format);
		}

		scratchImage.OverrideFormat(metadata.format);

		if (flags & texture_load_flags_gen_mips_on_cpu)
		{
			DirectX::ScratchImage mipchainImage;

			checkResult(DirectX::GenerateMipMaps(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, DirectX::TEX_FILTER_DEFAULT, 0, mipchainImage));
			scratchImage = std::move(mipchainImage);
			metadata = scratchImage.GetMetadata();
		}
		else
		{
			metadata.mipLevels = 1;
		}

		if (flags & texture_load_flags_premultiply_alpha)
		{
			DirectX::ScratchImage premultipliedAlphaImage;

			checkResult(DirectX::PremultiplyAlpha(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, DirectX::TEX_PMALPHA_DEFAULT, premultipliedAlphaImage));
			scratchImage = std::move(premultipliedAlphaImage);
			metadata = scratchImage.GetMetadata();
		}

		if (flags & texture_load_flags_compress)
		{
			DirectX::ScratchImage compressedImage;

			DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
			DXGI_FORMAT compressedFormat = DirectX::IsSRGB(metadata.format) ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;

			/*if (metadata.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
			{
				compressedFormat = DXGI_FORMAT_BC6H_UF16;
			}*/

			checkResult(DirectX::Compress(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata,
				compressedFormat, compressFlags, DirectX::TEX_THRESHOLD_DEFAULT, compressedImage));
			scratchImage = std::move(compressedImage);
			metadata = scratchImage.GetMetadata();
		}

		if (flags & texture_load_flags_cache_to_dds)
		{
			fs::create_directories(cacheFilepath.parent_path());
			checkResult(DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, DirectX::DDS_FLAGS_NONE, cacheFilepath.c_str()));
		}
	}

	if (flags & texture_load_flags_allocate_full_mipchain)
	{
		metadata.mipLevels = 0;
	}

	switch (metadata.dimension)
	{
	case DirectX::TEX_DIMENSION_TEXTURE1D:
		textureDesc = CD3DX12_RESOURCE_DESC::Tex1D(metadata.format, metadata.width, (uint16)metadata.arraySize, (uint16)metadata.mipLevels);
		break;
	case DirectX::TEX_DIMENSION_TEXTURE2D:
		textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(metadata.format, metadata.width, (uint32)metadata.height, (uint16)metadata.arraySize, (uint16)metadata.mipLevels);
		break;
	case DirectX::TEX_DIMENSION_TEXTURE3D:
		textureDesc = CD3DX12_RESOURCE_DESC::Tex3D(metadata.format, metadata.width, (uint32)metadata.height, (uint16)metadata.depth, (uint16)metadata.mipLevels);
		break;
	default:
		assert(false);
		break;
	}

	return true;
}

static ref<dx_texture> loadTextureInternal(const char* filename, uint32 flags)
{
	DirectX::ScratchImage scratchImage;
	D3D12_RESOURCE_DESC textureDesc;

	if (!loadImageFromFile(filename, flags, scratchImage, textureDesc))
	{
		return nullptr;
	}

	const DirectX::Image* images = scratchImage.GetImages();
	uint32 numImages = (uint32)scratchImage.GetImageCount();

	D3D12_SUBRESOURCE_DATA subresources[64];
	for (uint32 i = 0; i < numImages; ++i)
	{
		D3D12_SUBRESOURCE_DATA& subresource = subresources[i];
		subresource.RowPitch = images[i].rowPitch;
		subresource.SlicePitch = images[i].slicePitch;
		subresource.pData = images[i].pixels;
	}

	ref<dx_texture> result = createTexture(textureDesc, subresources, numImages);
	SET_NAME(result->resource, "Loaded from file");

	if (flags & texture_load_flags_gen_mips_on_gpu)
	{
		dxContext.renderQueue.waitForOtherQueue(dxContext.copyQueue);
		dx_command_list* cl = dxContext.getFreeRenderCommandList();
		generateMipMapsOnGPU(cl, result);
		dxContext.executeCommandList(cl);
	}

	return result;
}

ref<dx_texture> loadTextureFromFile(const char* filename, uint32 flags)
{
	static std::unordered_map<std::string, weakref<dx_texture>> textureCache; // TODO: Pack flags into key.
	static std::mutex mutex;

	mutex.lock();

	std::string s = filename;
	
	auto sp = textureCache[s].lock();
	if (!sp)
	{
		textureCache[s] = sp = loadTextureInternal(filename, flags);
	}

	mutex.unlock();
	return sp;
}

static bool checkFormatSupport(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport, D3D12_FORMAT_SUPPORT1 support)
{
	return (formatSupport.Support1 & support) != 0;
}

static bool checkFormatSupport(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport, D3D12_FORMAT_SUPPORT2 support)
{
	return (formatSupport.Support2 & support) != 0;
}

static bool formatSupportsRTV(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport)
{
	return checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
}

static bool formatSupportsDSV(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport)
{
	return checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
}

static bool formatSupportsSRV(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport)
{
	return checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
}

static bool formatSupportsUAV(D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport)
{
	return checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
		checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) &&
		checkFormatSupport(formatSupport, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
}

void uploadTextureSubresourceData(ref<dx_texture> texture, D3D12_SUBRESOURCE_DATA* subresourceData, uint32 firstSubresource, uint32 numSubresources)
{
	dx_command_list* cl = dxContext.getFreeCopyCommandList();
	cl->transitionBarrier(texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

	UINT64 requiredSize = GetRequiredIntermediateSize(texture->resource.Get(), firstSubresource, numSubresources);

	dx_resource intermediateResource;
	checkResult(dxContext.device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(requiredSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		0,
		IID_PPV_ARGS(&intermediateResource)
	));

	UpdateSubresources<128>(cl->commandList.Get(), texture->resource.Get(), intermediateResource.Get(), 0, firstSubresource, numSubresources, subresourceData);
	dxContext.retire(intermediateResource);

	// We are omitting the transition to common here, since the resource automatically decays to common state after being accessed on a copy queue.
	//cl->transitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	dxContext.executeCommandList(cl);
}

ref<dx_texture> createTexture(D3D12_RESOURCE_DESC textureDesc, D3D12_SUBRESOURCE_DATA* subresourceData, uint32 numSubresources, D3D12_RESOURCE_STATES initialState)
{
	ref<dx_texture> result = make_ref<dx_texture>();

	checkResult(dxContext.device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		initialState,
		0,
		IID_PPV_ARGS(&result->resource)));


	result->format = textureDesc.Format;
	result->width = (uint32)textureDesc.Width;
	result->height = textureDesc.Height;
	result->depth = textureDesc.DepthOrArraySize;

	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport;
	formatSupport.Format = textureDesc.Format;
	checkResult(dxContext.device->CheckFeatureSupport(
		D3D12_FEATURE_FORMAT_SUPPORT,
		&formatSupport,
		sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));

	result->supportsRTV = (textureDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) && formatSupportsRTV(formatSupport);
	result->supportsDSV = (textureDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && formatSupportsDSV(formatSupport);
	result->supportsUAV = (textureDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) && formatSupportsUAV(formatSupport);
	result->supportsSRV = formatSupportsSRV(formatSupport);

	result->defaultSRV = {};
	result->defaultUAV = {};
	result->rtvHandles = {};
	result->dsvHandle = {};
	result->stencilSRV = {};


	// Upload.
	if (subresourceData)
	{
		uploadTextureSubresourceData(result, subresourceData, 0, numSubresources);
	}

	// SRV.
	if (textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
	{
		result->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createVolumeTextureSRV(result);
	}
	else if (textureDesc.DepthOrArraySize == 6)
	{
		result->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createCubemapSRV(result);
	}
	else
	{
		result->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().create2DTextureSRV(result);
	}

	// RTV.
	if (result->supportsRTV)
	{
		result->rtvHandles = dxContext.rtvAllocator.getFreeHandle().create2DTextureRTV(result);
	}

	// UAV.
	if (result->supportsUAV)
	{
		if (textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		{
			result->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().createVolumeTextureUAV(result);
		}
		else if (textureDesc.DepthOrArraySize == 6)
		{
			result->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().createCubemapUAV(result);
		}
		else
		{
			result->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().create2DTextureUAV(result);
		}
	}

	return result;
}

ref<dx_texture> createTexture(const void* data, uint32 width, uint32 height, DXGI_FORMAT format, bool allocateMips, bool allowRenderTarget, bool allowUnorderedAccess, D3D12_RESOURCE_STATES initialState)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
		| (allowRenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE)
		| (allowUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE)
		;

	uint32 numMips = allocateMips ? 0 : 1;
	CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, numMips, 1, 0, flags);

	uint32 formatSize = getFormatSize(textureDesc.Format);

	if (data)
	{
		D3D12_SUBRESOURCE_DATA subresource;
		subresource.RowPitch = width * formatSize;
		subresource.SlicePitch = width * height * formatSize;
		subresource.pData = data;

		return createTexture(textureDesc, &subresource, 1, initialState);
	}
	else
	{
		return createTexture(textureDesc, 0, 0, initialState);
	}
}

ref<dx_texture> createDepthTexture(uint32 width, uint32 height, DXGI_FORMAT format, uint32 arrayLength, D3D12_RESOURCE_STATES initialState)
{
	ref<dx_texture> result = make_ref<dx_texture>();

	D3D12_CLEAR_VALUE optimizedClearValue = {};
	optimizedClearValue.Format = format;
	optimizedClearValue.DepthStencil = { 1.f, 0 };

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(getTypelessFormat(format), width, height,
		arrayLength, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	checkResult(dxContext.device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initialState,
		&optimizedClearValue,
		IID_PPV_ARGS(&result->resource)
	));


	result->format = format;
	result->width = width;
	result->height = height;
	result->depth = arrayLength;

	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport;
	formatSupport.Format = format;
	checkResult(dxContext.device->CheckFeatureSupport(
		D3D12_FEATURE_FORMAT_SUPPORT,
		&formatSupport,
		sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));

	result->supportsRTV = false;
	result->supportsDSV = formatSupportsDSV(formatSupport);
	result->supportsUAV = false;
	result->supportsSRV = formatSupportsSRV(formatSupport);

	result->defaultSRV = {};
	result->defaultUAV = {};
	result->rtvHandles = {};
	result->dsvHandle = {};
	result->stencilSRV = {};

	assert(result->supportsDSV);

	result->dsvHandle = dxContext.dsvAllocator.getFreeHandle().create2DTextureDSV(result);
	if (arrayLength == 1)
	{
		result->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createDepthTextureSRV(result);

		if (isStencilFormat(format))
		{
			result->stencilSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createStencilTextureSRV(result);
		}
	}
	else
	{
		result->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createDepthTextureArraySRV(result);
	}

	return result;
}

ref<dx_texture> createCubeTexture(const void* data, uint32 width, uint32 height, DXGI_FORMAT format, bool allocateMips, bool allowRenderTarget, bool allowUnorderedAccess, D3D12_RESOURCE_STATES initialState)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
		| (allowRenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE)
		| (allowUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE)
		;

	uint32 numMips = allocateMips ? 0 : 1;
	CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 6, numMips, 1, 0, flags);

	uint32 formatSize = getFormatSize(textureDesc.Format);

	if (data)
	{
		D3D12_SUBRESOURCE_DATA subresources[6];
		for (uint32 i = 0; i < 6; ++i)
		{
			auto& subresource = subresources[i];
			subresource.RowPitch = width * formatSize;
			subresource.SlicePitch = width * height * formatSize;
			subresource.pData = data;
		}

		return createTexture(textureDesc, subresources, 6, initialState);
	}
	else
	{
		return createTexture(textureDesc, 0, 0, initialState);
	}
}

ref<dx_texture> createVolumeTexture(const void* data, uint32 width, uint32 height, uint32 depth, DXGI_FORMAT format, bool allowUnorderedAccess, D3D12_RESOURCE_STATES initialState)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
		| (allowUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE)
		;

	CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex3D(format, width, height, depth, 1, flags);

	uint32 formatSize = getFormatSize(textureDesc.Format);

	if (data)
	{
		D3D12_SUBRESOURCE_DATA* subresources = (D3D12_SUBRESOURCE_DATA*)alloca(sizeof(D3D12_SUBRESOURCE_DATA) * depth);
		for (uint32 i = 0; i < depth; ++i)
		{
			auto& subresource = subresources[i];
			subresource.RowPitch = width * formatSize;
			subresource.SlicePitch = width * height * formatSize;
			subresource.pData = data;
		}

		return createTexture(textureDesc, subresources, depth, initialState);
	}
	else
	{
		return createTexture(textureDesc, 0, 0, initialState);
	}
}

void dx_texture::setName(const wchar* name)
{
	checkResult(resource->SetName(name));
}

std::wstring dx_texture::getName() const
{
	if (!resource)
	{
		return L"";
	}

	wchar name[128];
	uint32 size = sizeof(name); 
	resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, name); 
	name[min(arraysize(name) - 1, size)] = 0;

	return name;
}

static void retire(dx_resource resource, dx_cpu_descriptor_handle srv, dx_cpu_descriptor_handle uav, dx_cpu_descriptor_handle stencil, dx_rtv_descriptor_handle rtv, dx_dsv_descriptor_handle dsv)
{
	texture_grave grave;
	grave.resource = resource;
	grave.srv = srv;
	grave.uav = uav;
	grave.stencil = stencil;
	grave.rtv = rtv;
	grave.dsv = dsv;
	dxContext.retire(std::move(grave));
}

dx_texture::~dx_texture()
{
	retire(resource, defaultSRV, defaultUAV, stencilSRV, rtvHandles, dsvHandle);
}

void resizeTexture(ref<dx_texture> texture, uint32 newWidth, uint32 newHeight, D3D12_RESOURCE_STATES initialState)
{
	wchar name[128];
	uint32 size = sizeof(name);
	texture->resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, name);
	name[min(arraysize(name) - 1, size)] = 0;


	retire(texture->resource, texture->defaultSRV, texture->defaultUAV, texture->stencilSRV, texture->rtvHandles, texture->dsvHandle);

	D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
	texture->resource.Reset();


	D3D12_RESOURCE_STATES state = initialState;
	D3D12_CLEAR_VALUE optimizedClearValue = {};
	D3D12_CLEAR_VALUE* clearValue = 0;

	if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
	{
		state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		optimizedClearValue.Format = texture->format;
		optimizedClearValue.DepthStencil = { 1.f, 0 };
		clearValue = &optimizedClearValue;
	}

	desc.Width = newWidth;
	desc.Height = newHeight;
	texture->width = newWidth;
	texture->height = newHeight;

	if (desc.MipLevels != 1)
	{
		desc.MipLevels = 0;
	}

	checkResult(dxContext.device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		state,
		clearValue,
		IID_PPV_ARGS(&texture->resource)
	));

	// RTV.
	if (texture->supportsRTV)
	{
		texture->rtvHandles = dxContext.rtvAllocator.getFreeHandle().create2DTextureRTV(texture);
	}

	// DSV & SRV.
	if (texture->supportsDSV)
	{
		texture->dsvHandle = dxContext.dsvAllocator.getFreeHandle().create2DTextureDSV(texture);
		if (texture->depth == 1)
		{
			texture->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createDepthTextureSRV(texture);
		}
		else
		{
			texture->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createDepthTextureArraySRV(texture);
		}

		if (isStencilFormat(texture->format))
		{
			texture->stencilSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createStencilTextureSRV(texture);
		}
	}
	else
	{
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		{
			texture->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createVolumeTextureSRV(texture);
		}
		else if (texture->depth == 6)
		{
			texture->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().createCubemapSRV(texture);
		}
		else
		{
			texture->defaultSRV = dxContext.descriptorAllocatorCPU.getFreeHandle().create2DTextureSRV(texture);
		}
	}

	// UAV.
	if (texture->supportsUAV)
	{
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		{
			texture->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().createVolumeTextureUAV(texture);
		}
		else if (texture->depth == 6)
		{
			texture->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().createCubemapUAV(texture);
		}
		else
		{
			texture->defaultUAV = dxContext.descriptorAllocatorCPU.getFreeHandle().create2DTextureUAV(texture);
		}
	}

	texture->setName(name);
}

texture_grave::~texture_grave()
{
	wchar name[128];

	if (resource)
	{
		uint32 size = sizeof(name);
		resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, name);
		name[min(arraysize(name) - 1, size)] = 0;


		//std::cout << "Finally deleting texture." << std::endl;

		if (srv.cpuHandle.ptr)
		{
			dxContext.descriptorAllocatorCPU.freeHandle(srv);
		}
		if (uav.cpuHandle.ptr)
		{
			dxContext.descriptorAllocatorCPU.freeHandle(uav);
		}
		if (stencil.cpuHandle.ptr)
		{
			dxContext.descriptorAllocatorCPU.freeHandle(stencil);
		}
		if (rtv.cpuHandle.ptr)
		{
			dxContext.rtvAllocator.freeHandle(rtv);
		}
		if (dsv.cpuHandle.ptr)
		{
			dxContext.dsvAllocator.freeHandle(dsv);
		}
	}
}


