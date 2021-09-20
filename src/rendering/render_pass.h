#pragma once

#include "core/math.h"
#include "material.h"
#include "render_command.h"
#include "render_command_buffer.h"
#include "pbr.h"


struct opaque_render_pass
{
	void sort()
	{
		staticDepthPrepass.sort();
		dynamicDepthPrepass.sort();
		animatedDepthPrepass.sort();

		staticDoublesidedDepthPrepass.sort();
		dynamicDoublesidedDepthPrepass.sort();
		animatedDoublesidedDepthPrepass.sort();

		pass.sort();
	}

	void reset()
	{
		staticDepthPrepass.clear();
		dynamicDepthPrepass.clear();
		animatedDepthPrepass.clear();

		staticDoublesidedDepthPrepass.clear();
		dynamicDoublesidedDepthPrepass.clear();
		animatedDoublesidedDepthPrepass.clear();

		pass.clear();
	}

	template <typename pipeline_t>
	void renderStaticObject(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material,
		uint32 objectID = -1,
		bool doubleSided = false)
	{
		renderObjectCommon<pipeline_t>(transform, vertexBuffer, indexBuffer, submesh, material);

		float depth = 0.f; // TODO
		auto& buffer = doubleSided ? staticDoublesidedDepthPrepass : staticDepthPrepass;
		auto& depthCommand = buffer.emplace_back(depth);
		depthCommand.transform = transform;
		depthCommand.vertexBuffer = vertexBuffer.positions;
		depthCommand.indexBuffer = indexBuffer;
		depthCommand.submesh = submesh;
		depthCommand.objectID = objectID;
	}

	template <typename pipeline_t>
	void renderDynamicObject(const mat4& transform,
		const mat4& prevFrameTransform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material,
		uint32 objectID = -1,
		bool doubleSided = false)
	{
		renderObjectCommon<pipeline_t>(transform, vertexBuffer, indexBuffer, submesh, material);

		float depth = 0.f; // TODO
		auto& buffer = doubleSided ? dynamicDoublesidedDepthPrepass : dynamicDepthPrepass;
		auto& depthCommand = buffer.emplace_back(depth);
		depthCommand.transform = transform;
		depthCommand.prevFrameTransform = prevFrameTransform;
		depthCommand.vertexBuffer = vertexBuffer.positions;
		depthCommand.indexBuffer = indexBuffer;
		depthCommand.submesh = submesh;
		depthCommand.objectID = objectID;
	}

	template <typename pipeline_t>
	void renderAnimatedObject(const mat4& transform,
		const mat4& prevFrameTransform,
		dx_vertex_buffer_group_view& vertexBuffer,
		dx_vertex_buffer_group_view& prevFrameVertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material,
		uint32 objectID = -1,
		bool doubleSided = false)
	{
		renderObjectCommon<pipeline_t>(transform, vertexBuffer, indexBuffer, submesh, material);

		float depth = 0.f; // TODO
		auto& buffer = doubleSided ? animatedDoublesidedDepthPrepass : animatedDepthPrepass;
		auto& depthCommand = buffer.emplace_back(depth);
		depthCommand.transform = transform;
		depthCommand.prevFrameTransform = prevFrameTransform;
		depthCommand.vertexBuffer = vertexBuffer.positions;
		depthCommand.prevFrameVertexBufferAddress = prevFrameVertexBuffer.positions ? prevFrameVertexBuffer.positions.view.BufferLocation : vertexBuffer.positions.view.BufferLocation;
		depthCommand.indexBuffer = indexBuffer;
		depthCommand.submesh = submesh;
		depthCommand.objectID = objectID;
	}


	// Specializations for PBR materials, since these are the common ones.

	void renderStaticObject(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const ref<pbr_material>& material,
		uint32 objectID = -1)
	{
		if (material->doubleSided)
		{
			renderStaticObject<opaque_pbr_pipeline::double_sided>(transform, vertexBuffer, indexBuffer, submesh, material, objectID, true);
		}
		else
		{
			renderStaticObject<opaque_pbr_pipeline::standard>(transform, vertexBuffer, indexBuffer, submesh, material, objectID, false);
		}
	}

	void renderDynamicObject(const mat4& transform,
		const mat4& prevFrameTransform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const ref<pbr_material>& material,
		uint32 objectID = -1)
	{
		if (material->doubleSided)
		{
			renderDynamicObject<opaque_pbr_pipeline::double_sided>(transform, prevFrameTransform, vertexBuffer, indexBuffer, submesh, material, objectID, true);
		}
		else
		{
			renderDynamicObject<opaque_pbr_pipeline::standard>(transform, prevFrameTransform, vertexBuffer, indexBuffer, submesh, material, objectID, false);
		}
	}

	void renderAnimatedObject(const mat4& transform,
		const mat4& prevFrameTransform,
		dx_vertex_buffer_group_view& vertexBuffer,
		dx_vertex_buffer_group_view& prevFrameVertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const ref<pbr_material>& material,
		uint32 objectID = -1)
	{
		if (material->doubleSided)
		{
			renderAnimatedObject<opaque_pbr_pipeline::double_sided>(transform, prevFrameTransform, vertexBuffer, prevFrameVertexBuffer, indexBuffer, submesh, material, objectID, true);
		}
		else
		{
			renderAnimatedObject<opaque_pbr_pipeline::standard>(transform, prevFrameTransform, vertexBuffer, prevFrameVertexBuffer, indexBuffer, submesh, material, objectID, false);
		}
	}

	sort_key_vector<float, static_depth_only_render_command> staticDepthPrepass;
	sort_key_vector<float, dynamic_depth_only_render_command> dynamicDepthPrepass;
	sort_key_vector<float, animated_depth_only_render_command> animatedDepthPrepass;

	sort_key_vector<float, static_depth_only_render_command> staticDoublesidedDepthPrepass;
	sort_key_vector<float, dynamic_depth_only_render_command> dynamicDoublesidedDepthPrepass;
	sort_key_vector<float, animated_depth_only_render_command> animatedDoublesidedDepthPrepass;

	render_command_buffer<uint64> pass;

private:
	template <typename pipeline_t>
	void renderObjectCommon(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material)
	{
		using material_t = typename pipeline_t::material_t;

		uint64 sortKey = (uint64)pipeline_t::setup;
		auto& command = pass.emplace_back<pipeline_t, default_render_command<material_t>>(sortKey);
		command.transform = transform;
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.submesh = submesh;
		command.material = material;
	}
};

struct transparent_render_pass
{
	void sort()
	{
		pass.sort();
	}

	void reset()
	{
		pass.clear();
	}

	template <typename pipeline_t>
	void renderObject(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material)
	{
		using material_t = typename pipeline_t::material_t;

		float depth = 0.f; // TODO
		auto& command = pass.emplace_back<pipeline_t, default_render_command<material_t>>(-depth); // Negative depth -> sort from back to front.
		command.transform = transform;
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.submesh = submesh;
		command.material = material;
	}

	template <typename pipeline_t>
	void renderParticles(const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		const particle_draw_info& drawInfo,
		const typename pipeline_t::material_t& material)
	{
		using material_t = typename pipeline_t::material_t;

		float depth = 0.f; // TODO
		auto& command = pass.emplace_back<pipeline_t, particle_render_command<material_t>>(-depth); // Negative depth -> sort from back to front.
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.drawInfo = drawInfo;
		command.material = material;
	}

	void renderObject(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const ref<pbr_material>& material)
	{
		renderObject<transparent_pbr_pipeline>(transform, vertexBuffer, indexBuffer, submesh, material);
	}

	render_command_buffer<float> pass;
};

struct ldr_render_pass
{
	void sort()
	{
		ldrPass.sort();
		overlays.sort();
		// We don't sort the outlines.
	}

	void reset()
	{
		ldrPass.clear();
		overlays.clear();
		outlines.clear();
	}

	template <typename pipeline_t>
	void renderObject(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material)
	{
		using material_t = typename pipeline_t::material_t;

		float depth = 0.f; // TODO
		auto& command = ldrPass.emplace_back<pipeline_t, default_render_command<material_t>>(depth);
		command.transform = transform;
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.submesh = submesh;
		command.material = material;
	}

	template <typename pipeline_t>
	void renderOverlay(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh,
		const typename pipeline_t::material_t& material)
	{
		using material_t = typename pipeline_t::material_t;

		float depth = 0.f; // TODO
		auto& command = overlays.emplace_back<pipeline_t, default_render_command<material_t>>(depth);
		command.transform = transform;
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.submesh = submesh;
		command.material = material;
	}

	void renderOutline(const mat4& transform,
		const dx_vertex_buffer_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh)
	{
		auto& command = outlines.emplace_back();
		command.transform = transform;
		command.vertexBuffer = vertexBuffer;
		command.indexBuffer = indexBuffer;
		command.submesh = submesh;
	}

	void renderOutline(const mat4& transform,
		const dx_vertex_buffer_group_view& vertexBuffer,
		const dx_index_buffer_view& indexBuffer,
		submesh_info submesh)
	{
		renderOutline(transform, vertexBuffer.positions, indexBuffer, submesh);
	}

	render_command_buffer<float> ldrPass;
	render_command_buffer<float> overlays;
	std::vector<outline_render_command> outlines;
};





struct shadow_render_pass_base
{
	void renderStaticObject(const mat4& transform, const dx_vertex_buffer_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		auto& dcs = doubleSided ? doubleSidedStaticDrawCalls : staticDrawCalls;
		dcs.push_back(
			{
				transform,
				vertexBuffer,
				indexBuffer,
				submesh,
			}
		);
	}

	void renderDynamicObject(const mat4& transform, const dx_vertex_buffer_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		auto& dcs = doubleSided ? doubleSidedDynamicDrawCalls : dynamicDrawCalls;
		dcs.push_back(
			{
				transform,
				vertexBuffer,
				indexBuffer,
				submesh,
			}
		);
	}

	void renderStaticObject(const mat4& transform, const dx_vertex_buffer_group_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		renderStaticObject(transform, vertexBuffer.positions, indexBuffer, submesh);
	}

	void renderDynamicObject(const mat4& transform, const dx_vertex_buffer_group_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		renderDynamicObject(transform, vertexBuffer.positions, indexBuffer, submesh);
	}

	void reset()
	{
		staticDrawCalls.clear();
		dynamicDrawCalls.clear();
		doubleSidedStaticDrawCalls.clear();
		doubleSidedDynamicDrawCalls.clear();
	}

	std::vector<shadow_render_command> staticDrawCalls;
	std::vector<shadow_render_command> dynamicDrawCalls;

	std::vector<shadow_render_command> doubleSidedStaticDrawCalls;
	std::vector<shadow_render_command> doubleSidedDynamicDrawCalls;
};

struct sun_cascade_render_pass : shadow_render_pass_base
{
	mat4 viewProj;
	shadow_map_viewport viewport;
};

struct sun_shadow_render_pass
{
	// Since each cascade includes the next lower one, if you submit a draw to cascade N, it will also be rendered in N-1 automatically. No need to add it to the lower one.
	void renderStaticObject(uint32 cascadeIndex, const mat4& transform, const dx_vertex_buffer_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		cascades[cascadeIndex].renderStaticObject(transform, vertexBuffer, indexBuffer, submesh, doubleSided);
	}

	void renderStaticObject(uint32 cascadeIndex, const mat4& transform, const dx_vertex_buffer_group_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		renderStaticObject(cascadeIndex, transform, vertexBuffer.positions, indexBuffer, submesh, doubleSided);
	}

	void renderDynamicObject(uint32 cascadeIndex, const mat4& transform, const dx_vertex_buffer_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		cascades[cascadeIndex].renderDynamicObject(transform, vertexBuffer, indexBuffer, submesh, doubleSided);
	}

	void renderDynamicObject(uint32 cascadeIndex, const mat4& transform, const dx_vertex_buffer_group_view& vertexBuffer, const dx_index_buffer_view& indexBuffer, submesh_info submesh, bool doubleSided = false)
	{
		renderDynamicObject(cascadeIndex, transform, vertexBuffer.positions, indexBuffer, submesh, doubleSided);
	}

	void reset()
	{
		for (uint32 i = 0; i < MAX_NUM_SUN_SHADOW_CASCADES; ++i)
		{
			cascades[i].reset();
		}

		copyFromStaticCache = false;
	}

	sun_cascade_render_pass cascades[MAX_NUM_SUN_SHADOW_CASCADES];
	uint32 numCascades;
	bool copyFromStaticCache;
};

struct spot_shadow_render_pass : shadow_render_pass_base
{
	mat4 viewProjMatrix;
	shadow_map_viewport viewport;
	bool copyFromStaticCache;

	void reset()
	{
		shadow_render_pass_base::reset();
		copyFromStaticCache = false;
	}
};

// TODO: Split this into positive and negative direction for frustum culling.
struct point_shadow_render_pass : shadow_render_pass_base
{
	shadow_map_viewport viewport0;
	shadow_map_viewport viewport1;
	vec3 lightPosition;
	float maxDistance;
	bool copyFromStaticCache0;
	bool copyFromStaticCache1;

	void reset()
	{
		shadow_render_pass_base::reset();

		copyFromStaticCache0 = false;
		copyFromStaticCache1 = false;
	}
};



