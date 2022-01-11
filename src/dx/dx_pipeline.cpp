#include "pch.h"
#include "dx_pipeline.h"
#include "core/threading.h"
#include "core/string.h"
#include "core/file_system.h"
#include "core/log.h"

#include <unordered_map>
#include <set>
#include <deque>

static void handlePipelineChanges(const file_system_event& e);

struct shader_file
{
	dx_blob blob;
	std::set<struct reloadable_pipeline_state*> usedByPipelines;

	struct reloadable_root_signature* rootSignature;
};

enum desc_type
{
	desc_type_struct,
	desc_type_stream,
};

union graphics_pipeline_desc
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC structDesc;
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;

	graphics_pipeline_desc() = default;
};

enum pipeline_type
{
	pipeline_type_graphics,
	pipeline_type_compute,
};

struct reloadable_pipeline_state
{
	pipeline_type pipelineType;
	desc_type descriptionType;

	union
	{
		struct
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc;

			D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
			dx_pipeline_stream_base* stream;

			graphics_pipeline_files graphicsFiles;
		};

		struct
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc;
			const char* computeFile;
		};
	};

	dx_pipeline_state pipeline;
	dx_root_signature* rootSignature;

	D3D12_INPUT_ELEMENT_DESC inputLayout[16];

	reloadable_pipeline_state() {}


	void initialize(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const graphics_pipeline_files& files, dx_root_signature* rootSignature)
	{
		this->pipelineType = pipeline_type_graphics;
		this->descriptionType = desc_type_struct;
		this->graphicsDesc = desc;
		this->graphicsFiles = files;
		this->rootSignature = rootSignature;

		assert(desc.InputLayout.NumElements <= arraysize(inputLayout));

		memcpy(inputLayout, desc.InputLayout.pInputElementDescs, sizeof(D3D12_INPUT_ELEMENT_DESC) * desc.InputLayout.NumElements);
		this->graphicsDesc.InputLayout.pInputElementDescs = inputLayout;

		if (desc.InputLayout.NumElements == 0)
		{
			this->graphicsDesc.InputLayout.pInputElementDescs = nullptr;
		}
	}

	void initialize(const D3D12_PIPELINE_STATE_STREAM_DESC& desc, dx_pipeline_stream_base* stream, const graphics_pipeline_files& files, dx_root_signature* rootSignature)
	{
		this->pipelineType = pipeline_type_graphics;
		this->descriptionType = desc_type_stream;
		this->streamDesc = desc;
		this->stream = stream;
		this->graphicsFiles = files;
		this->rootSignature = rootSignature;

		// TODO: Handle input layout. For now we expect the user to use one of the globally defined formats.
	}

	void initialize(const char* file, dx_root_signature* rootSignature)
	{
		this->pipelineType = pipeline_type_compute;
		this->computeDesc = {};
		this->computeFile = file;
		this->rootSignature = rootSignature;
	}
};

struct reloadable_root_signature
{
	const char* file;
	dx_root_signature rootSignature;
};


static std::unordered_map<std::string, shader_file> shaderBlobs;
static std::deque<reloadable_pipeline_state> pipelines;
static std::deque<reloadable_root_signature> rootSignaturesFromFiles;
static std::deque<dx_root_signature> userRootSignatures;


static std::vector<reloadable_pipeline_state*> dirtyPipelines;
static std::vector<reloadable_root_signature*> dirtyRootSignatures;
static std::mutex mutex;


static reloadable_root_signature* pushBlob(const char* filename, reloadable_pipeline_state* pipelineIndex, bool isRootSignature = false)
{
	reloadable_root_signature* result = 0;

	if (filename)
	{
		auto it = shaderBlobs.find(filename);
		if (it == shaderBlobs.end())
		{
			// New file.

			std::wstring filepath = SHADER_BIN_DIR + stringToWstring(filename) + L".cso";

			dx_blob blob;
			checkResult(D3DReadFileToBlob(filepath.c_str(), &blob));

			if (isRootSignature)
			{
				rootSignaturesFromFiles.push_back({ filename, nullptr });
				result = &rootSignaturesFromFiles.back();
			}

			mutex.lock();
			shaderBlobs[filename] = { blob, { pipelineIndex }, result };
			mutex.unlock();
		}
		else
		{
			// Already used.

			mutex.lock();
			it->second.usedByPipelines.insert(pipelineIndex);
			mutex.unlock();

			if (isRootSignature)
			{
				if (!it->second.rootSignature)
				{
					rootSignaturesFromFiles.push_back({ filename, nullptr });
					it->second.rootSignature = &rootSignaturesFromFiles.back();
				}

				result = it->second.rootSignature;
			}
		}
	}

	return result;
}

dx_pipeline createReloadablePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const graphics_pipeline_files& files,
	dx_root_signature userRootSignature)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	pushBlob(files.vs, &state);
	pushBlob(files.ps, &state);
	pushBlob(files.gs, &state);
	pushBlob(files.ds, &state);
	pushBlob(files.hs, &state);

	assert(!files.ms);
	assert(!files.as);

	userRootSignatures.push_back(userRootSignature);
	dx_root_signature* rootSignature = &userRootSignatures.back();
	userRootSignatures.back() = userRootSignature; // Fuck. You.

	state.initialize(desc, files, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

dx_pipeline createReloadablePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const graphics_pipeline_files& files, rs_file rootSignatureFile)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	reloadable_root_signature* reloadableRS = pushBlob(files.shaders[rootSignatureFile], &state, true);
	pushBlob(files.vs, &state);
	pushBlob(files.ps, &state);
	pushBlob(files.gs, &state);
	pushBlob(files.ds, &state);
	pushBlob(files.hs, &state);

	assert(!files.ms);
	assert(!files.as);

	dx_root_signature* rootSignature = &reloadableRS->rootSignature;

	state.initialize(desc, files, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

dx_pipeline createReloadablePipeline(const char* csFile, dx_root_signature userRootSignature)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	pushBlob(csFile, &state);

	userRootSignatures.push_back(userRootSignature);
	dx_root_signature* rootSignature = &userRootSignatures.back();
	userRootSignatures.back() = userRootSignature; // Fuck. You.

	state.initialize(csFile, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

dx_pipeline createReloadablePipeline(const char* csFile)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	reloadable_root_signature* reloadableRS = pushBlob(csFile, &state, true);

	dx_root_signature* rootSignature = &reloadableRS->rootSignature;

	state.initialize(csFile, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

dx_pipeline createReloadablePipeline(const D3D12_PIPELINE_STATE_STREAM_DESC& desc, dx_pipeline_stream_base* stream, const graphics_pipeline_files& files, 
	dx_root_signature userRootSignature)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	pushBlob(files.vs, &state);
	pushBlob(files.ps, &state);
	pushBlob(files.gs, &state);
	pushBlob(files.ds, &state);
	pushBlob(files.hs, &state);
	pushBlob(files.ms, &state);
	pushBlob(files.as, &state);

	userRootSignatures.push_back(userRootSignature);
	dx_root_signature* rootSignature = &userRootSignatures.back();
	userRootSignatures.back() = userRootSignature; // Fuck. You.

	state.initialize(desc, stream, files, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

dx_pipeline createReloadablePipeline(const D3D12_PIPELINE_STATE_STREAM_DESC& desc, dx_pipeline_stream_base* stream, const graphics_pipeline_files& files, rs_file rootSignatureFile)
{
	pipelines.emplace_back();
	auto& state = pipelines.back();

	reloadable_root_signature* reloadableRS = pushBlob(files.shaders[rootSignatureFile], &state, true);
	pushBlob(files.vs, &state);
	pushBlob(files.ps, &state);
	pushBlob(files.gs, &state);
	pushBlob(files.ds, &state);
	pushBlob(files.hs, &state);
	pushBlob(files.ms, &state);
	pushBlob(files.as, &state);

	dx_root_signature* rootSignature = &reloadableRS->rootSignature;

	state.initialize(desc, stream, files, rootSignature);

	dx_pipeline result = { &state.pipeline, rootSignature };
	return result;
}

static void loadRootSignature(reloadable_root_signature& r)
{
	dx_blob rs = shaderBlobs[r.file].blob;

	dxContext.retire(r.rootSignature.rootSignature); 
	freeRootSignature(r.rootSignature);
	r.rootSignature = createRootSignature(rs);
}

static void loadPipeline(reloadable_pipeline_state& p)
{
	dxContext.retire(p.pipeline);

	if (p.pipelineType == pipeline_type_graphics)
	{
		if (p.descriptionType == desc_type_struct)
		{
			if (p.graphicsFiles.vs) { dx_blob shader = shaderBlobs[p.graphicsFiles.vs].blob; p.graphicsDesc.VS = CD3DX12_SHADER_BYTECODE(shader.Get()); }
			if (p.graphicsFiles.ps) { dx_blob shader = shaderBlobs[p.graphicsFiles.ps].blob; p.graphicsDesc.PS = CD3DX12_SHADER_BYTECODE(shader.Get()); }
			if (p.graphicsFiles.gs) { dx_blob shader = shaderBlobs[p.graphicsFiles.gs].blob; p.graphicsDesc.GS = CD3DX12_SHADER_BYTECODE(shader.Get()); }
			if (p.graphicsFiles.ds) { dx_blob shader = shaderBlobs[p.graphicsFiles.ds].blob; p.graphicsDesc.DS = CD3DX12_SHADER_BYTECODE(shader.Get()); }
			if (p.graphicsFiles.hs) { dx_blob shader = shaderBlobs[p.graphicsFiles.hs].blob; p.graphicsDesc.HS = CD3DX12_SHADER_BYTECODE(shader.Get()); }

			p.graphicsDesc.pRootSignature = p.rootSignature->rootSignature.Get();
			checkResult(dxContext.device->CreateGraphicsPipelineState(&p.graphicsDesc, IID_PPV_ARGS(&p.pipeline)));
		}
		else
		{
			if (p.graphicsFiles.vs) { dx_blob shader = shaderBlobs[p.graphicsFiles.vs].blob; p.stream->setVertexShader(shader); }
			if (p.graphicsFiles.ps) { dx_blob shader = shaderBlobs[p.graphicsFiles.ps].blob; p.stream->setPixelShader(shader); }
			if (p.graphicsFiles.gs) { dx_blob shader = shaderBlobs[p.graphicsFiles.gs].blob; p.stream->setGeometryShader(shader); }
			if (p.graphicsFiles.ds) { dx_blob shader = shaderBlobs[p.graphicsFiles.ds].blob; p.stream->setDomainShader(shader); }
			if (p.graphicsFiles.hs) { dx_blob shader = shaderBlobs[p.graphicsFiles.hs].blob; p.stream->setHullShader(shader); }
			if (p.graphicsFiles.ms) { dx_blob shader = shaderBlobs[p.graphicsFiles.ms].blob; p.stream->setMeshShader(shader); }
			if (p.graphicsFiles.as) { dx_blob shader = shaderBlobs[p.graphicsFiles.as].blob; p.stream->setAmplificationShader(shader); }

			p.stream->setRootSignature(*p.rootSignature);
			checkResult(dxContext.device->CreatePipelineState(&p.streamDesc, IID_PPV_ARGS(&p.pipeline)));
		}
	}
	else
	{
		dx_blob shader = shaderBlobs[p.computeFile].blob; p.computeDesc.CS = CD3DX12_SHADER_BYTECODE(shader.Get());

		p.computeDesc.pRootSignature = p.rootSignature->rootSignature.Get();
		checkResult(dxContext.device->CreateComputePipelineState(&p.computeDesc, IID_PPV_ARGS(&p.pipeline)));
	}
}

#define MULTI_THREADED_CREATION 0

void createAllPendingReloadablePipelines()
{
	static int rsOffset = 0;
	static int pipelineOffset = 0;

	thread_job_context context;
	for (uint32 i = rsOffset; i < (uint32)rootSignaturesFromFiles.size(); ++i)
	{
#if MULTI_THREADED_CREATION
		context.addWork([i]()
		{
#endif
			loadRootSignature(rootSignaturesFromFiles[i]);
#if MULTI_THREADED_CREATION
		});
#endif
	}
	context.waitForWorkCompletion();

	for (uint32 i = pipelineOffset; i < (uint32)pipelines.size(); ++i)
	{
#if MULTI_THREADED_CREATION
		context.addWork([i]()
		{
#endif
			loadPipeline(pipelines[i]);
#if MULTI_THREADED_CREATION
		});
#endif
	}
	context.waitForWorkCompletion();

	rsOffset = (int)rootSignaturesFromFiles.size();
	pipelineOffset = (int)pipelines.size();

	//static HANDLE thread = CreateThread(0, 0, checkForFileChanges, 0, 0, 0); // Static, so that we only do this once.
	static bool observing = observeDirectory(SHADER_BIN_DIR, handlePipelineChanges); // Static, so that we only do this once.
}

static bool fileIsLocked(const wchar* filename)
{
	HANDLE fileHandle = CreateFileW(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		return true;
	}
	CloseHandle(fileHandle);
	return false;
}

static void handlePipelineChanges(const file_system_event& e)
{
	if (e.change == file_system_change_modify)
	{
		bool isFile = !fs::is_directory(e.path);

		if (isFile)
		{
			mutex.lock();
			auto it = shaderBlobs.find(e.path.stem().string());
			if (it != shaderBlobs.end())
			{
				mutex.unlock();
				auto wPath = e.path.wstring();
				while (fileIsLocked(wPath.c_str()))
				{
					// Wait.
				}

				uint32 numDirtyPipelines = (uint32)it->second.usedByPipelines.size();
				LOG_MESSAGE("Reloading shader blob '%ws'. Updating %u pipeline%s.", e.path.stem().c_str(), numDirtyPipelines, (numDirtyPipelines > 1 ? "s" : ""));

				dx_blob blob;
				checkResult(D3DReadFileToBlob(e.path.c_str(), &blob));

				mutex.lock();
				it->second.blob = blob;
				dirtyPipelines.insert(dirtyPipelines.end(), it->second.usedByPipelines.begin(), it->second.usedByPipelines.end());
				if (it->second.rootSignature)
				{
					dirtyRootSignatures.push_back(it->second.rootSignature);
				}
				mutex.unlock();
			}
			else
			{
				mutex.unlock();
			}
		}
	}
}

void checkForChangedPipelines()
{
	mutex.lock();

	thread_job_context context;
	for (uint32 i = 0; i < (uint32)dirtyRootSignatures.size(); ++i)
	{
#if MULTI_THREADED_CREATION
		context.addWork([i]()
		{
#endif
			loadRootSignature(*dirtyRootSignatures[i]);
#if MULTI_THREADED_CREATION
		});
#endif
	}
	context.waitForWorkCompletion();

	for (uint32 i = 0; i < (uint32)dirtyPipelines.size(); ++i)
	{
#if MULTI_THREADED_CREATION
		context.addWork([i]()
		{
#endif
			loadPipeline(*dirtyPipelines[i]);
#if MULTI_THREADED_CREATION
		});
#endif
	}
	context.waitForWorkCompletion();

	dirtyRootSignatures.clear();
	dirtyPipelines.clear();
	mutex.unlock();
}

static void copyRootSignatureDesc(const D3D12_ROOT_SIGNATURE_DESC* desc, dx_root_signature& result)
{
	result.totalNumParameters = desc->NumParameters;

	uint32 numDescriptorTables = 0;
	for (uint32 i = 0; i < desc->NumParameters; ++i)
	{
		if (desc->pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			++numDescriptorTables;
			setBit(result.tableRootParameterMask, i);
		}
	}

	result.descriptorTableSizes = new uint32[numDescriptorTables];
	result.numDescriptorTables = numDescriptorTables;

	uint32 index = 0;
	for (uint32 i = 0; i < desc->NumParameters; ++i)
	{
		if (desc->pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			uint32 numRanges = desc->pParameters[i].DescriptorTable.NumDescriptorRanges;
			result.descriptorTableSizes[index] = 0;
			for (uint32 r = 0; r < numRanges; ++r)
			{
				result.descriptorTableSizes[index] += desc->pParameters[i].DescriptorTable.pDescriptorRanges[r].NumDescriptors;
			}
			++index;
		}
	}
}

static void copyRootSignatureDesc(const D3D12_ROOT_SIGNATURE_DESC1* desc, dx_root_signature& result)
{
	result.totalNumParameters = desc->NumParameters;

	uint32 numDescriptorTables = 0;
	for (uint32 i = 0; i < desc->NumParameters; ++i)
	{
		if (desc->pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			++numDescriptorTables;
			setBit(result.tableRootParameterMask, i);
		}
	}

	result.descriptorTableSizes = new uint32[numDescriptorTables];
	result.numDescriptorTables = numDescriptorTables;

	uint32 index = 0;
	for (uint32 i = 0; i < desc->NumParameters; ++i)
	{
		if (desc->pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			uint32 numRanges = desc->pParameters[i].DescriptorTable.NumDescriptorRanges;
			result.descriptorTableSizes[index] = 0;
			for (uint32 r = 0; r < numRanges; ++r)
			{
				result.descriptorTableSizes[index] += desc->pParameters[i].DescriptorTable.pDescriptorRanges[r].NumDescriptors;
			}
			++index;
		}
	}
}

dx_root_signature createRootSignature(dx_blob rootSignatureBlob)
{
	dx_root_signature result = {};

	checkResult(dxContext.device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
		rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&result.rootSignature)));

	com<ID3D12RootSignatureDeserializer> deserializer;
	checkResult(D3D12CreateRootSignatureDeserializer(rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&deserializer)));
	D3D12_ROOT_SIGNATURE_DESC* desc = (D3D12_ROOT_SIGNATURE_DESC*)deserializer->GetRootSignatureDesc();

	copyRootSignatureDesc(desc, result);

	return result;
}

dx_root_signature createRootSignature(const wchar* path)
{
	dx_blob rootSignatureBlob;
	checkResult(D3DReadFileToBlob(path, &rootSignatureBlob));
	return createRootSignature(rootSignatureBlob);
}

dx_root_signature createRootSignature(const D3D12_ROOT_SIGNATURE_DESC1& desc)
{
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(dxContext.device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init_1_1(desc.NumParameters, desc.pParameters, desc.NumStaticSamplers, desc.pStaticSamplers, desc.Flags);

	dx_blob rootSignatureBlob;
	dx_blob errorBlob;
	checkResult(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));

	dx_root_signature rootSignature = {};

	checkResult(dxContext.device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
		rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature.rootSignature)));

	copyRootSignatureDesc(&desc, rootSignature);

	return rootSignature;
}

dx_root_signature createRootSignature(CD3DX12_ROOT_PARAMETER1* rootParameters, uint32 numRootParameters, CD3DX12_STATIC_SAMPLER_DESC* samplers, uint32 numSamplers,
	D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
	rootSignatureDesc.Flags = flags;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = numRootParameters;
	rootSignatureDesc.pStaticSamplers = samplers;
	rootSignatureDesc.NumStaticSamplers = numSamplers;
	return createRootSignature(rootSignatureDesc);
}

dx_root_signature createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
	dx_blob rootSignatureBlob;
	dx_blob errorBlob;
	checkResult(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignatureBlob, &errorBlob));

	dx_root_signature rootSignature = {};

	checkResult(dxContext.device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
		rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature.rootSignature)));

	copyRootSignatureDesc(&desc, rootSignature);

	return rootSignature;
}

dx_root_signature createRootSignature(CD3DX12_ROOT_PARAMETER* rootParameters, uint32 numRootParameters, CD3DX12_STATIC_SAMPLER_DESC* samplers, uint32 numSamplers,
	D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = flags;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = numRootParameters;
	rootSignatureDesc.pStaticSamplers = samplers;
	rootSignatureDesc.NumStaticSamplers = numSamplers;
	return createRootSignature(rootSignatureDesc);
}

dx_root_signature createRootSignature(D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = flags;
	return createRootSignature(rootSignatureDesc);
}

void freeRootSignature(dx_root_signature& rs)
{
	if (rs.descriptorTableSizes)
	{
		delete[] rs.descriptorTableSizes;
	}
}

dx_command_signature createCommandSignature(dx_root_signature rootSignature, const D3D12_COMMAND_SIGNATURE_DESC& commandSignatureDesc)
{
	dx_command_signature commandSignature;
	checkResult(dxContext.device->CreateCommandSignature(&commandSignatureDesc,
		commandSignatureDesc.NumArgumentDescs == 1 ? 0 : rootSignature.rootSignature.Get(),
		IID_PPV_ARGS(&commandSignature)));
	return commandSignature;
}

dx_command_signature createCommandSignature(dx_root_signature rootSignature, D3D12_INDIRECT_ARGUMENT_DESC* argumentDescs, uint32 numArgumentDescs, uint32 commandStructureSize)
{
	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc;
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = numArgumentDescs;
	commandSignatureDesc.ByteStride = commandStructureSize;
	commandSignatureDesc.NodeMask = 0;

	return createCommandSignature(rootSignature, commandSignatureDesc);
}


