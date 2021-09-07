#include "pch.h"
#include "raytracing.h"
#include "dx/dx_context.h"
#include "dx/dx_command_list.h"

#include <dxcapi.h>
#include <fstream>
#include <sstream>

raytracing_blas_builder& raytracing_blas_builder::push(vertex_buffer_group vertexBuffer, ref<dx_index_buffer> indexBuffer, submesh_info submesh, bool opaque, const trs& localTransform)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc;

	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc.Flags = opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

	if (&localTransform == &trs::identity)
	{
		geomDesc.Triangles.Transform3x4 = UINT64_MAX;
	}
	else
	{
		geomDesc.Triangles.Transform3x4 = localTransforms.size();
		localTransforms.push_back(transpose(trsToMat4(localTransform)));
	}

	geomDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.positions->gpuVirtualAddress + (vertexBuffer.positions->elementSize * submesh.baseVertex);
	geomDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.positions->elementSize;
	geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geomDesc.Triangles.VertexCount = submesh.numVertices;

	geomDesc.Triangles.IndexBuffer = indexBuffer->gpuVirtualAddress + (indexBuffer->elementSize * submesh.firstIndex);
	geomDesc.Triangles.IndexFormat = getIndexBufferFormat(indexBuffer->elementSize);
	geomDesc.Triangles.IndexCount = submesh.numIndices;

	geometryDescs.push_back(geomDesc);
	geometries.push_back({ raytracing_mesh_geometry, vertexBuffer, indexBuffer, submesh });

	return *this;
}

raytracing_blas_builder& raytracing_blas_builder::push(const std::vector<bounding_box>& boundingBoxes, bool opaque)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc;

	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
	geomDesc.Flags = opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

	for (uint32 i = 0; i < (uint32)boundingBoxes.size(); ++i)
	{
		D3D12_RAYTRACING_AABB& w = aabbDescs.emplace_back();
		const bounding_box& r = boundingBoxes[i];

		w.MinX = r.minCorner.x;
		w.MinY = r.minCorner.y;
		w.MinZ = r.minCorner.z;

		w.MaxX = r.maxCorner.x;
		w.MaxY = r.maxCorner.y;
		w.MaxZ = r.maxCorner.z;
	}

	geomDesc.AABBs.AABBCount = boundingBoxes.size();

	geometryDescs.push_back(geomDesc);
	geometries.push_back({ raytracing_procedural_geometry });

	return *this;
}

ref<raytracing_blas> raytracing_blas_builder::finish(bool keepScratch)
{	
	dx_dynamic_constant_buffer localTransformsBuffer = {};
	if (localTransforms.size() > 0)
	{
		localTransformsBuffer = dxContext.uploadDynamicConstantBuffer(sizeof(mat4) * (uint32)localTransforms.size(), localTransforms.data()).first;
	}

	dx_dynamic_constant_buffer aabbBuffer = {};
	if (aabbDescs.size())
	{
		aabbBuffer = dxContext.uploadDynamicConstantBuffer(sizeof(D3D12_RAYTRACING_AABB) * (uint32)aabbDescs.size(), aabbDescs.data()).first;
	}

	uint64 aabbOffset = 0;

	for (auto& desc : geometryDescs)
	{
		if (desc.Type == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
		{
			if (desc.Triangles.Transform3x4 == UINT64_MAX)
			{
				desc.Triangles.Transform3x4 = 0;
			}
			else
			{
				desc.Triangles.Transform3x4 = localTransformsBuffer.gpuPtr + sizeof(mat4) * desc.Triangles.Transform3x4;
			}
		}
		else if (desc.Type == D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS)
		{
			desc.AABBs.AABBs.StartAddress = aabbBuffer.gpuPtr + sizeof(D3D12_RAYTRACING_AABB) * aabbOffset;
			desc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
			aabbOffset += desc.AABBs.AABBCount;
		}
	}




	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

	inputs.NumDescs = (uint32)geometryDescs.size();
	inputs.pGeometryDescs = geometryDescs.data();

	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;



	// Allocate.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	dxContext.device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	info.ScratchDataSizeInBytes = alignTo(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	info.ResultDataMaxSizeInBytes = alignTo(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	ref<raytracing_blas> blas = make_ref<raytracing_blas>();
	blas->scratch = createBuffer((uint32)info.ScratchDataSizeInBytes, 1, 0, true, false, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	blas->blas = createBuffer((uint32)info.ResultDataMaxSizeInBytes, 1, 0, true, false, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	SET_NAME(blas->scratch->resource, "BLAS Scratch");
	SET_NAME(blas->blas->resource, "BLAS Result");

	blas->geometries = std::move(geometries);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = inputs;
	asDesc.DestAccelerationStructureData = blas->blas->gpuVirtualAddress;
	asDesc.ScratchAccelerationStructureData = blas->scratch->gpuVirtualAddress;

	dx_command_list* cl = dxContext.getFreeComputeCommandList(true);
	cl->commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, 0);
	cl->uavBarrier(blas->blas);
	dxContext.executeCommandList(cl);

	if (!keepScratch)
	{
		blas->scratch = 0;
	}

	return blas;
}


static void reportShaderCompileError(com<IDxcBlobEncoding> blob)
{
	char infoLog[2048];
	memcpy(infoLog, blob->GetBufferPointer(), sizeof(infoLog) - 1);
	infoLog[sizeof(infoLog) - 1] = 0;
	std::cerr << "Error: " << infoLog << '\n';
}

static com<IDxcBlob> compileLibrary(const std::wstring& filename, const std::vector<const wchar*>& shaderNameDefines)
{
	com<IDxcCompiler> compiler;
	com<IDxcLibrary> library;
	com<IDxcIncludeHandler> includeHandler;
	checkResult(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
	checkResult(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)));
	checkResult(library->CreateIncludeHandler(&includeHandler));


	std::ifstream stream(filename);
	if (!stream.is_open())
	{
		//std::cerr <<  "File " << filename << " not found." << '\n';
		return 0;
	}
	std::stringstream ss; ss << stream.rdbuf();
	std::string source = ss.str();


	// Create blob from the string.
	com<IDxcBlobEncoding> textBlob;
	checkResult(library->CreateBlobWithEncodingFromPinned((LPBYTE)source.c_str(), (uint32)source.length(), 0, &textBlob));

	std::wstring wfilename(filename.begin(), filename.end());

	std::vector<DxcDefine> defines;

	std::vector<std::wstring> valueStrings;
	valueStrings.reserve(1 + shaderNameDefines.size()); // Important. We pull out raw char pointers from these, so the vector should not reallocate.

	valueStrings.push_back(std::to_wstring(shaderNameDefines.size()));
	defines.push_back({ L"NUM_RAY_TYPES", valueStrings.back().c_str() });

	for (uint32 i = 0; i < shaderNameDefines.size(); ++i)
	{
		valueStrings.push_back(std::to_wstring(i));
		defines.push_back({ shaderNameDefines[i], valueStrings.back().c_str() });
	}


	defines.push_back({ L"HLSL" });
	defines.push_back({ L"mat4",	L"float4x4" });
	defines.push_back({ L"vec2",	L"float2" });
	defines.push_back({ L"vec3",	L"float3" });
	defines.push_back({ L"vec4",	L"float4" });
	defines.push_back({ L"uint32",	L"uint" });


	// Compile.
	com<IDxcOperationResult> operationResult;
	checkResult(compiler->Compile(textBlob.Get(), wfilename.c_str(), L"", L"lib_6_3", 0, 0, defines.data(), (uint32)defines.size(), includeHandler.Get(), &operationResult));

	// Verify the result.
	HRESULT resultCode;
	checkResult(operationResult->GetStatus(&resultCode));
	if (FAILED(resultCode))
	{
		com<IDxcBlobEncoding> error;
		checkResult(operationResult->GetErrorBuffer(&error));
		reportShaderCompileError(error);
		assert(false);
		return 0;
	}

	com<IDxcBlob> blob;
	checkResult(operationResult->GetResult(&blob));

	return blob;
}





raytracing_pipeline_builder::raytracing_pipeline_builder(const wchar* shaderFilename, uint32 payloadSize, uint32 maxRecursionDepth, bool hasMeshGeometry, bool hasProceduralGeometry)
{
	this->shaderFilename = shaderFilename;
	this->payloadSize = payloadSize;
	this->maxRecursionDepth = maxRecursionDepth;
	this->hasMeshGeometry = hasMeshGeometry;
	this->hasProceduralGeometry = hasProceduralGeometry;

	assert(hasMeshGeometry || hasProceduralGeometry);
}

raytracing_pipeline_builder::raytracing_root_signature raytracing_pipeline_builder::createRaytracingRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
	raytracing_root_signature result;
	result.rootSignature = createRootSignature(desc);
	result.rootSignaturePtr = result.rootSignature.rootSignature.Get();
	return result;
}

raytracing_pipeline_builder& raytracing_pipeline_builder::globalRootSignature(D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc)
{
	assert(!globalRS.rootSignature.rootSignature);

	globalRS = createRaytracingRootSignature(rootSignatureDesc);
	SET_NAME(globalRS.rootSignature.rootSignature, "Global raytracing root signature");

	auto& so = subobjects[numSubobjects++];
	so.pDesc = &globalRS.rootSignaturePtr;
	so.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;

	return *this;
}

static uint32 getShaderBindingTableSize(const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc)
{
	uint32 size = 0;
	for (uint32 i = 0; i < rootSignatureDesc.NumParameters; ++i)
	{
		if (rootSignatureDesc.pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
		{
			size += alignTo(rootSignatureDesc.pParameters[i].Constants.Num32BitValues * 4, 8);
		} 
		else
		{
			size += 8;
		}
	}
	return size;
}

raytracing_pipeline_builder& raytracing_pipeline_builder::raygen(const wchar* entryPoint, D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc)
{
	assert(!raygenRS.rootSignature.rootSignature);

	D3D12_EXPORT_DESC& exp = exports[numExports++];
	exp.Name = entryPoint;
	exp.Flags = D3D12_EXPORT_FLAG_NONE;
	exp.ExportToRename = 0;

	raygenEntryPoint = entryPoint;


	rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	raygenRS = createRaytracingRootSignature(rootSignatureDesc);
	SET_NAME(raygenRS.rootSignature.rootSignature, "Local raytracing root signature");

	{
		auto& so = subobjects[numSubobjects++];
		so.pDesc = &raygenRS.rootSignaturePtr;
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	}

	{
		auto& so = subobjects[numSubobjects++];
		auto& as = associations[numAssociations++];

		stringBuffer[numStrings++] = entryPoint;

		as.NumExports = 1;
		as.pExports = &stringBuffer[numStrings - 1];
		as.pSubobjectToAssociate = &subobjects[numSubobjects - 2];

		so.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		so.pDesc = &as;
	}

	allExports.push_back(entryPoint);

	uint32 size = getShaderBindingTableSize(rootSignatureDesc);
	tableEntrySize = max(size, tableEntrySize);

	return *this;
}

raytracing_pipeline_builder& raytracing_pipeline_builder::hitgroup(const wchar* groupName, const wchar* miss,
	raytracing_mesh_hitgroup mesh, D3D12_ROOT_SIGNATURE_DESC meshRootSignatureDesc,
	raytracing_procedural_hitgroup procedural, D3D12_ROOT_SIGNATURE_DESC proceduralRootSignatureDesc)
{
	auto exportEntryPoint = [this](const wchar* entryPoint)
	{
		D3D12_EXPORT_DESC& exp = exports[numExports++];
		exp.Name = entryPoint;
		exp.Flags = D3D12_EXPORT_FLAG_NONE;
		exp.ExportToRename = 0;

		allExports.push_back(entryPoint);
	};

	auto createLocalRootSignature = [this](D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc, const wchar* closestHit, const wchar* anyHit, const wchar* intersection)
	{
		if (rootSignatureDesc.NumParameters > 0)
		{
			rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
			rootSignatures[numRootSignatures++] = createRaytracingRootSignature(rootSignatureDesc);

			{
				auto& so = subobjects[numSubobjects++];
				so.pDesc = &rootSignatures[numRootSignatures - 1].rootSignaturePtr;
				so.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			}

			{
				auto& so = subobjects[numSubobjects++];
				auto& as = associations[numAssociations++];

				const wchar** entryPoints = &stringBuffer[numStrings];
				uint32 numEntryPoints = 0;

				if (closestHit) { entryPoints[numEntryPoints++] = closestHit; }
				if (anyHit) { entryPoints[numEntryPoints++] = anyHit; }
				if (intersection) { entryPoints[numEntryPoints++] = intersection; }

				numStrings += numEntryPoints;

				as.NumExports = numEntryPoints;
				as.pExports = entryPoints;
				as.pSubobjectToAssociate = &subobjects[numSubobjects - 2];

				so.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
				so.pDesc = &as;
			}

			uint32 size = getShaderBindingTableSize(rootSignatureDesc);
			tableEntrySize = max(size, tableEntrySize);
		}
	};

	if (hasMeshGeometry)
	{
		groupNameStorage[groupNameStoragePtr++] = std::wstring(groupName) + L"_MESH";
		const wchar* name = groupNameStorage[groupNameStoragePtr - 1].c_str();

		D3D12_HIT_GROUP_DESC& hitGroup = hitGroups[numHitGroups++];

		hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		hitGroup.AnyHitShaderImport = mesh.anyHit;
		hitGroup.ClosestHitShaderImport = mesh.closestHit;
		hitGroup.IntersectionShaderImport = 0;
		hitGroup.HitGroupExport = name;

		auto& so = subobjects[numSubobjects++];
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		so.pDesc = &hitGroup;

		if (mesh.closestHit) { exportEntryPoint(mesh.closestHit); }
		if (mesh.anyHit) { exportEntryPoint(mesh.anyHit); }

		createLocalRootSignature(meshRootSignatureDesc, mesh.closestHit, mesh.anyHit, 0);
	}

	if (hasProceduralGeometry)
	{
		groupNameStorage[groupNameStoragePtr++] = std::wstring(groupName) + L"_PROC";
		const wchar* name = groupNameStorage[groupNameStoragePtr - 1].c_str();

		D3D12_HIT_GROUP_DESC& hitGroup = hitGroups[numHitGroups++];

		hitGroup.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
		hitGroup.AnyHitShaderImport = procedural.anyHit;
		hitGroup.ClosestHitShaderImport = procedural.closestHit;
		hitGroup.IntersectionShaderImport = procedural.intersection;
		hitGroup.HitGroupExport = name;

		auto& so = subobjects[numSubobjects++];
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		so.pDesc = &hitGroup;

		assert(procedural.intersection);

		if (procedural.closestHit) { exportEntryPoint(procedural.closestHit); }
		if (procedural.intersection) { exportEntryPoint(procedural.intersection); }
		if (procedural.anyHit) { exportEntryPoint(procedural.anyHit); }

		createLocalRootSignature(meshRootSignatureDesc, procedural.closestHit, procedural.anyHit, procedural.intersection);
	}

	exportEntryPoint(miss);
	emptyAssociations.push_back(miss);
	missEntryPoints.push_back(miss);

	shaderNameDefines.push_back(groupName);

	return *this;
}

dx_raytracing_pipeline raytracing_pipeline_builder::finish()
{
	assert(raygenRS.rootSignature.rootSignature);
	assert(globalRS.rootSignature.rootSignature);

	auto shaderBlob = compileLibrary(shaderFilename, shaderNameDefines);

	D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
	dxilLibDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
	dxilLibDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
	dxilLibDesc.NumExports = numExports;
	dxilLibDesc.pExports = exports;

	D3D12_ROOT_SIGNATURE_DESC emptyRootSignatureDesc = {};
	emptyRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	raytracing_root_signature emptyRootSignature = createRaytracingRootSignature(emptyRootSignatureDesc);

	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;

	{
		D3D12_STATE_SUBOBJECT& so = subobjects[numSubobjects++];
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		so.pDesc = &dxilLibDesc;
	}

	{
		D3D12_STATE_SUBOBJECT& so = subobjects[numSubobjects++];
		so.pDesc = &emptyRootSignature.rootSignaturePtr;
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	}

	{
		auto& so = subobjects[numSubobjects++];
		auto& as = associations[numAssociations++];

		as.NumExports = (uint32)emptyAssociations.size();
		as.pExports = emptyAssociations.data();
		as.pSubobjectToAssociate = &subobjects[numSubobjects - 2];

		so.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		so.pDesc = &as;
	}

	{
		shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2; // 2 floats for the BuiltInTriangleIntersectionAttributes.
		shaderConfig.MaxPayloadSizeInBytes = payloadSize;

		auto& so = subobjects[numSubobjects++];
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		so.pDesc = &shaderConfig;
	}

	{
		auto& so = subobjects[numSubobjects++];
		auto& as = associations[numAssociations++];

		as.NumExports = (uint32)allExports.size();
		as.pExports = allExports.data();
		as.pSubobjectToAssociate = &subobjects[numSubobjects - 2];

		so.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		so.pDesc = &as;
	}

	{
		pipelineConfig.MaxTraceRecursionDepth = maxRecursionDepth;

		auto& so = subobjects[numSubobjects++];
		so.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		so.pDesc = &pipelineConfig;
	}


	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = numSubobjects;
	desc.pSubobjects = subobjects;
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	dx_raytracing_pipeline result;
	checkResult(dxContext.device->CreateStateObject(&desc, IID_PPV_ARGS(&result.pipeline)));
	result.rootSignature = globalRS.rootSignature;


	for (uint32 i = 0; i < numRootSignatures; ++i)
	{
		freeRootSignature(rootSignatures[i].rootSignature);
	}
	freeRootSignature(raygenRS.rootSignature);


	com<ID3D12StateObjectProperties> rtsoProps;
	result.pipeline->QueryInterface(IID_PPV_ARGS(&rtsoProps));


	{
		auto& shaderBindingTableDesc = result.shaderBindingTableDesc;
		tableEntrySize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		shaderBindingTableDesc.entrySize = (uint32)alignTo(tableEntrySize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

		uint32 numGeometryTypes = hasMeshGeometry + hasProceduralGeometry;

		uint32 numRaygenShaderEntries = 1;
		uint32 numMissShaderEntries = numHitGroups / numGeometryTypes;

		shaderBindingTableDesc.raygen = rtsoProps->GetShaderIdentifier(raygenEntryPoint);

		uint32 numUniqueGroups = (uint32)missEntryPoints.size();
		assert(numUniqueGroups * numGeometryTypes == numHitGroups);

		for (uint32 i = 0; i < numUniqueGroups; ++i)
		{
			shaderBindingTableDesc.miss.push_back(rtsoProps->GetShaderIdentifier(missEntryPoints[i]));

			raytracing_shader& shader = shaderBindingTableDesc.hitGroups.emplace_back();
			shader = { };
			if (hasMeshGeometry)
			{
				shader.mesh = rtsoProps->GetShaderIdentifier(hitGroups[numGeometryTypes * i].HitGroupExport);
			}
			if (hasProceduralGeometry)
			{
				shader.procedural = rtsoProps->GetShaderIdentifier(hitGroups[numGeometryTypes * i + numGeometryTypes - 1].HitGroupExport);
			}
		}


		shaderBindingTableDesc.raygenOffset = 0;
		shaderBindingTableDesc.missOffset = shaderBindingTableDesc.raygenOffset + (uint32)alignTo(numRaygenShaderEntries * tableEntrySize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		shaderBindingTableDesc.hitOffset = shaderBindingTableDesc.missOffset + (uint32)alignTo(numMissShaderEntries * tableEntrySize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	}

	return result;
}

