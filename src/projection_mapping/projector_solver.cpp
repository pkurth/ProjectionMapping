#include "pch.h"
#include "projector_solver.h"
#include "rendering/render_utils.h"

#include "dx/dx_pipeline.h"
#include "dx/dx_descriptor_allocation.h"
#include "dx/dx_profiling.h"
#include "dx/dx_barrier_batcher.h"

#include "projector_rs.hlsli"
#include "transform.hlsli"

static dx_pipeline solverPipeline;
static dx_pipeline regularizePipeline;

static dx_pipeline confidencePipeline;
static dx_pipeline bestMaskPipeline;
static dx_pipeline intensitiesPipeline;

static dx_pipeline simulationPipeline;


void projector_solver::initialize()
{
	solverPipeline = createReloadablePipeline("projector_solver_cs");
	regularizePipeline = createReloadablePipeline("projector_regularize_cs");

	confidencePipeline = createReloadablePipeline("projector_confidence_cs");
	bestMaskPipeline = createReloadablePipeline("projector_best_mask_cs");
	intensitiesPipeline = createReloadablePipeline("projector_intensities_cs");

	{
		auto desc = CREATE_GRAPHICS_PIPELINE
			.inputLayout(inputLayout_position_uv_normal_tangent)
			.renderTargets(opaqueLightPassFormats, arraysize(opaqueLightPassFormats), depthStencilFormat)
			.depthSettings(true, false, D3D12_COMPARISON_FUNC_EQUAL);

		simulationPipeline = createReloadablePipeline(desc, { "default_vs", "projector_simulation_ps" });
	}

	for (uint32 i = 0; i < NUM_BUFFERED_FRAMES; ++i)
	{
		heaps[i].initialize(1024);
	}
}

static void projectorToCB(const render_camera& camera, projector_cb& proj)
{
	proj.viewProj = camera.viewProj;
	proj.invViewProj = camera.invViewProj;
	proj.position = vec4(camera.position, 1.f);
	proj.screenDims = vec2((float)camera.width, (float)camera.height);
	proj.invScreenDims = 1.f / proj.screenDims;
	proj.projectionParams = camera.getShaderProjectionParams();
	proj.forward = vec4(camera.rotation * vec3(0.f, 0.f, -1.f), 0.f);
}

void projector_solver::solve(const projector_component* projectors, uint32 numProjectors)
{
	this->numProjectors = numProjectors;

	dx_pushable_descriptor_heap& heap = heaps[dxContext.bufferedFrameID];
	heap.reset();

	dx_allocation alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(projector_cb));
	projector_cb* projectorCBs = (projector_cb*)alloc.cpuPtr;
	projectorsGPUAddress = alloc.gpuPtr;

	alloc = dxContext.allocateDynamicBuffer(numProjectors * sizeof(projector_cb));
	projector_cb* realProjectorCBs = (projector_cb*)alloc.cpuPtr;
	realProjectorsGPUAddress = alloc.gpuPtr;


	for (uint32 i = 0; i < arraysize(descriptors); ++i)
	{
		descriptors[i].cpuHandle = heap.baseCPU + i * numProjectors;
		descriptors[i].gpuHandle = heap.baseGPU + i * numProjectors;
	}

	for (uint32 i = 0; i < numProjectors; ++i)
	{
		const projector_component& p = projectors[i];

		projectorToCB(p.calibratedCamera, projectorCBs[i]);
		projectorToCB(p.realCamera, realProjectorCBs[i]);

		(linearRenderResultsBaseDescriptor + i).create2DTextureSRV(p.renderer.ldrPostProcessingTexture);
		(srgbRenderResultsBaseDescriptor + i).create2DTextureSRV(p.renderer.frameResult);
		(worldNormalsBaseDescriptor + i).create2DTextureSRV(p.renderer.worldNormalsTexture);
		(depthTexturesBaseDescriptor + i).createDepthTextureSRV(p.renderer.depthStencilBuffer);
		(realDepthTexturesBaseDescriptor + i).createDepthTextureSRV(p.renderer.realDepthStencilBuffer);
		(bestMaskSRVBaseDescriptor + i).create2DTextureSRV(p.renderer.bestMaskTexture);
		(bestMaskUAVBaseDescriptor + i).create2DTextureUAV(p.renderer.bestMaskTexture);
		(depthDiscontinuitiesTexturesBaseDescriptor + i).create2DTextureSRV(p.renderer.depthDiscontinuitiesTexture);
		(colorDiscontinuitiesTexturesBaseDescriptor + i).create2DTextureSRV(p.renderer.colorDiscontinuitiesTexture);
		(intensitiesSRVBaseDescriptor + i).create2DTextureSRV(p.renderer.solverIntensityTexture);
		(intensitiesUAVBaseDescriptor + i).create2DTextureUAV(p.renderer.solverIntensityTexture);
		(tempIntensitiesSRVBaseDescriptor + i).create2DTextureSRV(p.renderer.solverIntensityTempTexture);
		(tempIntensitiesUAVBaseDescriptor + i).create2DTextureUAV(p.renderer.solverIntensityTempTexture);
		(confidencesSRVBaseDescriptor + i).create2DTextureSRV(p.renderer.confidenceTexture);
		(confidencesUAVBaseDescriptor + i).create2DTextureUAV(p.renderer.confidenceTexture);
	}





	dx_command_list* cl = dxContext.getFreeRenderCommandList();

#if 0

	for (uint32 iter = 0; iter < numIterationsPerFrame; ++iter)
	{
		DX_PROFILE_BLOCK(cl, "Solver iteration");

		for (uint32 proj = 0; proj < numProjectors; ++proj)
		{
			DX_PROFILE_BLOCK(cl, "Single projector");


			uint32 width = widths[proj];
			uint32 height = heights[proj];


			// Solve intensity.

			cl->setPipelineState(*solverPipeline.pipeline);
			cl->setComputeRootSignature(*solverPipeline.rootSignature);

			cl->setRootComputeSRV(PROJECTOR_SOLVER_RS_VIEWPROJS, projectorsGPUAddress);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_RENDER_RESULTS, linearRenderResultsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_WORLD_NORMALS, worldNormalsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_INTENSITIES, intensitiesSRVBaseDescriptor); // Read from regularized intensities.
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_MASKS, depthDiscontinuitiesTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_SOLVER_RS_OUT_INTENSITIES, tempIntensitiesUAVBaseDescriptor); // Write to temp intensities.

			projector_solver_cb solverCB;
			solverCB.currentIndex = proj;
			solverCB.numProjectors = numProjectors;
			solverCB.referenceDistance = referenceDistance;
			solverCB.maskStrength = depthDiscontinuityMaskStrength;

			cl->transitionBarrier(intensityTempTextures[proj]->resource, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cl->setCompute32BitConstants(PROJECTOR_SOLVER_RS_CB, solverCB);

			cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));

			barrier_batcher(cl)
				//.uav(intensityTempTextures[proj]->resource)
				.transition(intensityTempTextures[proj]->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)
				.transition(intensityTextures[proj]->resource, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);



			// Regularize.

			cl->setPipelineState(*regularizePipeline.pipeline);
			cl->setComputeRootSignature(*regularizePipeline.rootSignature);

			projector_regularize_cb regularizeCB;
			regularizeCB.currentIndex = proj;
			regularizeCB.strength = regularizationStrength;

			cl->setCompute32BitConstants(PROJECTOR_REGULARIZE_RS_CB, regularizeCB);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_INTENSITIES, tempIntensitiesSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_MASKS, depthDiscontinuitiesTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_REGULARIZE_RS_OUT_INTENSITIES, intensitiesUAVBaseDescriptor);

			cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));

			barrier_batcher(cl)
				//.uav(intensityTextures[proj]->resource)
				.transition(intensityTextures[proj]->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

#else


	{
		PROFILE_ALL(cl, "Projector solver");


		{
			PROFILE_ALL(cl, "Prepare masks");

			{
				PROFILE_ALL(cl, "Depth masks");

				for (uint32 i = 0; i < numProjectors; ++i)
				{
					const ref<dx_texture>& depthStencilBuffer = projectors[i].renderer.depthStencilBuffer;
					const ref<dx_texture>& halfResolutionDepthBuffer = projectors[i].renderer.halfResolutionDepthBuffer;
					const ref<dx_texture>& depthDiscontinuitiesTexture = projectors[i].renderer.depthDiscontinuitiesTexture;
					const ref<dx_texture>& dilateTempTexture = projectors[i].renderer.dilateTempTexture;
					vec4 projectionParams = projectors[i].renderer.projectorCamera.projectionParams;

					barrier_batcher(cl)
						.transition(depthStencilBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					depthPyramid(cl, depthStencilBuffer, halfResolutionDepthBuffer);

					barrier_batcher(cl)
						//.uav(halfResolutionDepthBuffer)
						.transition(halfResolutionDepthBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
						.transition(depthStencilBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

					depthSobel(cl, halfResolutionDepthBuffer, depthDiscontinuitiesTexture, projectionParams, depthDiscontinuityThreshold);

					barrier_batcher(cl)
						//.uav(depthDiscontinuitiesTexture)
						.transition(depthDiscontinuitiesTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
						.transition(halfResolutionDepthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					dilate(cl, depthDiscontinuitiesTexture, dilateTempTexture, depthDiscontinuityDilateRadius);
					dilateSmooth(cl, depthDiscontinuitiesTexture, dilateTempTexture, depthDiscontinuitySmoothRadius);
					gaussianBlur(cl, depthDiscontinuitiesTexture, dilateTempTexture, 0, 0, gaussian_blur_5x5);

					barrier_batcher(cl)
						//.uav(depthDiscontinuitiesTexture)
						.transition(depthDiscontinuitiesTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}
			}

			{
				PROFILE_ALL(cl, "Color masks");

				for (uint32 i = 0; i < numProjectors; ++i)
				{
					const ref<dx_texture>& ldrPostProcessingTexture = projectors[i].renderer.ldrPostProcessingTexture;
					const ref<dx_texture>& halfResolutionColorTexture = projectors[i].renderer.halfResolutionColorTexture;
					const ref<dx_texture>& colorDiscontinuitiesTexture = projectors[i].renderer.colorDiscontinuitiesTexture;
					const ref<dx_texture>& dilateTempTexture = projectors[i].renderer.dilateTempTexture;

					blit(cl, ldrPostProcessingTexture, halfResolutionColorTexture);

					barrier_batcher(cl)
						//.uav(halfResolutionColorTexture)
						.transition(halfResolutionColorTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					colorSobel(cl, halfResolutionColorTexture, colorDiscontinuitiesTexture, colorDiscontinuityThreshold);

					barrier_batcher(cl)
						//.uav(colorDiscontinuitiesTexture)
						.transition(halfResolutionColorTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
						.transition(colorDiscontinuitiesTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					dilate(cl, colorDiscontinuitiesTexture, dilateTempTexture, colorDiscontinuityDilateRadius);
					dilateSmooth(cl, colorDiscontinuitiesTexture, dilateTempTexture, colorDiscontinuitySmoothRadius);
					gaussianBlur(cl, colorDiscontinuitiesTexture, dilateTempTexture, 0, 0, gaussian_blur_13x13, 4);

					barrier_batcher(cl)
						//.uav(colorDiscontinuitiesTexture)
						.transition(colorDiscontinuitiesTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}
			}

		}








		cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);




		{
			barrier_batcher batch(cl);
			for (uint32 i = 0; i < numProjectors; ++i)
			{
				batch.transition(projectors[i].renderer.confidenceTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}
		}

		{
			PROFILE_ALL(cl, "Confidences");

			cl->setPipelineState(*confidencePipeline.pipeline);
			cl->setComputeRootSignature(*confidencePipeline.rootSignature);

			cl->setRootComputeSRV(PROJECTOR_CONFIDENCE_RS_PROJECTORS, projectorsGPUAddress);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_RENDER_RESULTS, linearRenderResultsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_WORLD_NORMALS, worldNormalsBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_INTENSITIES, intensitiesSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_DEPTH_MASKS, depthDiscontinuitiesTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_COLOR_MASKS, colorDiscontinuitiesTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_CONFIDENCE_RS_OUTPUT, confidencesUAVBaseDescriptor);

			for (uint32 i = 0; i < numProjectors; ++i)
			{
				uint32 width = projectors[i].renderer.renderWidth;
				uint32 height = projectors[i].renderer.renderHeight;

				projector_confidence_cb cb;
				cb.index = i;
				cb.desiredWhiteValue = 0.7f;
				cb.referenceDistance = referenceDistance;

				cl->setCompute32BitConstants(PROJECTOR_CONFIDENCE_RS_CB, cb);

				cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));
			}
		}

		{
			barrier_batcher batch(cl);
			for (uint32 i = 0; i < numProjectors; ++i)
			{
				batch.transition(projectors[i].renderer.confidenceTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
				batch.transition(projectors[i].renderer.solverIntensityTexture, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				batch.transition(projectors[i].renderer.bestMaskTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); // TODO: Delete
			}
		}

#if 0
		{
			PROFILE_ALL(cl, "Best mask");

			cl->setPipelineState(*bestMaskPipeline.pipeline);
			cl->setComputeRootSignature(*bestMaskPipeline.rootSignature);

			cl->setRootComputeSRV(PROJECTOR_BEST_MASK_RS_PROJECTORS, projectorsGPUAddress);
			cl->setComputeDescriptorTable(PROJECTOR_BEST_MASK_RS_CONFIDENCES, confidencesSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_BEST_MASK_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_BEST_MASK_RS_OUT_MASK, bestMaskUAVBaseDescriptor);

			for (uint32 i = 0; i < numProjectors; ++i)
			{
				uint32 width = projectors[i].renderer.renderWidth;
				uint32 height = projectors[i].renderer.renderHeight;

				projector_best_mask_cb cb;
				cb.index = i;
				cb.numProjectors = numProjectors;

				cl->setCompute32BitConstants(PROJECTOR_BEST_MASK_RS_CB, cb);

				cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));
			}
		}

		{
			barrier_batcher batch(cl);
			for (uint32 i = 0; i < numProjectors; ++i)
			{
				batch.transition(projectors[i].renderer.bestMaskTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}
		}

		{
			cl->resetToDynamicDescriptorHeap();
			PROFILE_ALL(cl, "Blur best masks");
			for (uint32 i = 0; i < numProjectors; ++i)
			{
				gaussianBlur(cl, projectors[i].renderer.bestMaskTexture, projectors[i].renderer.dilateTempTexture, 0, 0, gaussian_blur_9x9, 5);
			}
			cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);
		}
#endif

		{
			PROFILE_ALL(cl, "Intensities");

			cl->setPipelineState(*intensitiesPipeline.pipeline);
			cl->setComputeRootSignature(*intensitiesPipeline.rootSignature);

			cl->setRootComputeSRV(PROJECTOR_INTENSITIES_RS_PROJECTORS, projectorsGPUAddress);
			cl->setComputeDescriptorTable(PROJECTOR_INTENSITIES_RS_CONFIDENCES, confidencesSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_INTENSITIES_RS_DEPTH_TEXTURES, depthTexturesBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_INTENSITIES_RS_BEST_MASKS, bestMaskSRVBaseDescriptor);
			cl->setComputeDescriptorTable(PROJECTOR_INTENSITIES_RS_OUT_INTENSITIES, intensitiesUAVBaseDescriptor);

			for (uint32 i = 0; i < numProjectors; ++i)
			{
				uint32 width = projectors[i].renderer.renderWidth;
				uint32 height = projectors[i].renderer.renderHeight;

				projector_intensity_cb cb;
				cb.index = i;
				cb.numProjectors = numProjectors;

				cl->setCompute32BitConstants(PROJECTOR_INTENSITIES_RS_CB, cb);

				cl->dispatch(bucketize(width, PROJECTOR_BLOCK_SIZE), bucketize(height, PROJECTOR_BLOCK_SIZE));
			}
		}

		{
			barrier_batcher batch(cl);
			for (uint32 i = 0; i < numProjectors; ++i)
			{
				batch.transition(projectors[i].renderer.solverIntensityTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
				batch.transition(projectors[i].renderer.bestMaskTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); // TODO: Delete
			}
		}
	}


#endif

	cl->resetToDynamicDescriptorHeap();

	dxContext.executeCommandList(cl);
}













struct visualization_material 
{
	projector_solver* solver;
};


struct simulate_projectors_pipeline
{
	using material_t = visualization_material;

	PIPELINE_SETUP_DECL;
	PIPELINE_RENDER_DECL;
};

PIPELINE_SETUP_IMPL(simulate_projectors_pipeline)
{
	cl->setPipelineState(*simulationPipeline.pipeline);
	cl->setGraphicsRootSignature(*simulationPipeline.rootSignature);
	cl->setPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

PIPELINE_RENDER_IMPL(simulate_projectors_pipeline)
{
	projector_solver& solver = *rc.material.solver;

	dx_pushable_descriptor_heap& heap = solver.heaps[dxContext.bufferedFrameID];

	cl->setDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap.descriptorHeap);

	cl->setGraphics32BitConstants(PROJECTOR_SIMULATION_RS_TRANSFORM, transform_cb{ viewProj * rc.transform, rc.transform });
	cl->setGraphics32BitConstants(PROJECTOR_SIMULATION_RS_CB, projector_visualization_cb{ solver.numProjectors, solver.referenceDistance });
	cl->setRootGraphicsSRV(PROJECTOR_SIMULATION_RS_VIEWPROJS, solver.realProjectorsGPUAddress);
	cl->setGraphicsDescriptorTable(PROJECTOR_SIMULATION_RS_RENDER_RESULTS, solver.srgbRenderResultsBaseDescriptor);
	cl->setGraphicsDescriptorTable(PROJECTOR_SIMULATION_RS_DEPTH_TEXTURES, solver.realDepthTexturesBaseDescriptor);


	cl->setVertexBuffer(0, rc.vertexBuffer.positions);
	cl->setVertexBuffer(1, rc.vertexBuffer.others);
	cl->setIndexBuffer(rc.indexBuffer);
	cl->drawIndexed(rc.submesh.numIndices, 1, rc.submesh.firstIndex, rc.submesh.baseVertex, 0);

	cl->resetToDynamicDescriptorHeap();
}

void projector_solver::simulateProjectors(opaque_render_pass* opaqueRenderPass,
	const mat4& transform,
	const dx_vertex_buffer_group_view& vertexBuffer,
	const dx_index_buffer_view& indexBuffer,
	submesh_info submesh,
	uint32 objectID)
{
	opaqueRenderPass->renderStaticObject<simulate_projectors_pipeline>(
		transform, vertexBuffer, indexBuffer,
		submesh, { this },
		objectID
		);
}
