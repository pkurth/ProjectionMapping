#pragma once

//#define ENTT_ID_TYPE uint64
//#include <entt/entt.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/helper.hpp>
#include "components.h"
#ifndef PHYSICS_ONLY
#include "rendering/light_source.h"
#include "rendering/pbr.h"
#include "core/camera.h"
#endif

struct game_scene;

struct scene_entity
{
	scene_entity() = default;
	inline scene_entity(entt::entity handle, game_scene& scene);
	inline scene_entity(uint32 id, game_scene& scene);
	scene_entity(entt::entity handle, entt::registry* registry) : handle(handle), registry(registry) {}
	scene_entity(uint32 id, entt::registry* reg) : handle((entt::entity)id), registry(reg) {}
	scene_entity(const scene_entity&) = default;


	bool isValid()
	{
		return registry && registry->valid(handle);
	}

	template <typename component_t, typename... args>
	scene_entity& addComponent(args&&... a)
	{
		auto& component = registry->emplace_or_replace<component_t>(handle, std::forward<args>(a)...);
		return *this;
	}

	template <typename component_t>
	bool hasComponent()
	{
		return registry->any_of<component_t>(handle);
	}

	template <typename component_t>
	component_t& getComponent()
	{
		return registry->get<component_t>(handle);
	}

	template <typename component_t>
	const component_t& getComponent() const
	{
		return registry->get<component_t>(handle);
	}

	template <typename component_t>
	component_t* getComponentIfExists()
	{
		return registry->try_get<component_t>(handle);
	}

	template <typename component_t>
	const component_t* getComponentIfExists() const
	{
		return registry->try_get<component_t>(handle);
	}

	template <typename component_t>
	uint32 getComponentIndex() const
	{
		auto pool = registry->pool_if_exists<component_t>();
		assert(pool);
		return (uint32)pool->index(handle);
	}

	template <typename component_t>
	uint32 getComponentIndexIfExists() const
	{
		auto pool = registry->pool_if_exists<component_t>();
		return (pool && pool->contains(handle)) ? (uint32)pool->index(handle) : (uint32)-1;
	}

	template <typename component_t>
	void removeComponent()
	{
		registry->remove<component_t>(handle);
	}

	inline operator uint32() const
	{
		return (uint32)handle;
	}

	inline operator bool() const
	{
		return handle != entt::null;
	}

	inline bool operator==(const scene_entity& o) const
	{
		return handle == o.handle && registry == o.registry;
	}

	inline bool operator!=(const scene_entity& o) const
	{
		return !(*this == o);
	}

	inline bool operator==(entt::entity o) const
	{
		return handle == o;
	}

	entt::entity handle = entt::null;
	entt::registry* registry;
};

struct game_scene
{
	game_scene();

	scene_entity createEntity(const char* name)
	{
		return scene_entity(registry.create(), &registry)
			.addComponent<tag_component>(name);
	}

	scene_entity copyEntity(scene_entity src); // Source can be either from the same scene or from another.

	void deleteEntity(scene_entity e);
	void clearAll();

	template <typename component_t>
	void deleteAllComponents()
	{
		registry.clear<component_t>();
	}

	template <typename component_t>
	scene_entity getEntityFromComponent(const component_t& c)
	{
		entt::entity e = entt::to_entity(registry, c);
		return { e, &registry };
	}

	template <typename component_t>
	void copyComponentIfExists(scene_entity src, scene_entity dst)
	{
		if (component_t* comp = src.getComponentIfExists<component_t>())
		{
			dst.addComponent<component_t>(*comp);
		}
	}

	template <typename first_component_t, typename... tail_component_t>
	void copyComponentsIfExists(scene_entity src, scene_entity dst)
	{
		copyComponentIfExists<first_component_t>(src, dst);
		if constexpr (sizeof...(tail_component_t) > 0)
		{
			copyComponentsIfExists<tail_component_t...>(src, dst);
		}
	}

	template <typename... component_t>
	auto view() 
	{ 
		return registry.view<component_t...>(); 
	}

	template<typename... owned_component_t, typename... Get, typename... Exclude>
	auto group(entt::get_t<Get...> = {}, entt::exclude_t<Exclude...> = {})
	{
		return registry.group<owned_component_t...>(entt::get<Get...>, entt::exclude<Exclude...>);
	}

	template <typename component_t>
	auto raw()
	{
		component_t** r = registry.view<component_t>().raw();
		return r ? *r : 0;
	}

	template <typename func_t>
	void forEachEntity(func_t func)
	{
		registry.each(func);
	}

	template <typename component_t>
	uint32 numberOfComponentsOfType()
	{
		return (uint32)registry.size<component_t>();
	}

	template <typename component_t>
	component_t& getComponentAtIndex(uint32 index)
	{
		auto pool = registry.pool_if_exists<component_t>();
		assert(pool);
		return pool->element_at(index);
	}

	template <typename component_t>
	scene_entity getEntityFromComponentAtIndex(uint32 index)
	{
		return getEntityFromComponent(getComponentAtIndex<component_t>(index));
	}

	template <typename context_t, typename... args>
	context_t& createOrGetContextVariable(args&&... a)
	{
		return registry.ctx_or_set<context_t>(std::forward<args>(a)...);
	}

	template <typename context_t>
	context_t& getContextVariable()
	{
		return registry.ctx<context_t>();
	}

	template <typename context_t>
	bool doesContextVariableExist()
	{
		return registry.try_ctx<context_t>() != 0;
	}

	template <typename context_t>
	void deleteContextVariable()
	{
		registry.unset<context_t>();
	}

	entt::registry registry;


	fs::path savePath;

#ifndef PHYSICS_ONLY
	render_camera camera;
	directional_light sun;
	ref<pbr_environment> environment;
#endif
};

inline scene_entity::scene_entity(entt::entity handle, game_scene& scene) : handle(handle), registry(&scene.registry) {}
inline scene_entity::scene_entity(uint32 id, game_scene& scene) : handle((entt::entity)id), registry(&scene.registry) {}
