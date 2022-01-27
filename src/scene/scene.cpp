#include "pch.h"
#include "scene.h"

game_scene::game_scene()
{
	// Construct groups early. Ingore the return types.

#ifndef PHYSICS_ONLY
	(void)registry.group<position_component, point_light_component>();
	(void)registry.group<position_rotation_component, spot_light_component>();
#endif
}

void game_scene::clearAll()
{
	registry.clear();
}

scene_entity game_scene::copyEntity(scene_entity src)
{
	assert(src.hasComponent<tag_component>());

	tag_component& tag = src.getComponent<tag_component>();
	scene_entity dest = createEntity(tag.name);

	if (auto* c = src.getComponentIfExists<transform_component>()) { dest.addComponent<transform_component>(*c); }
	if (auto* c = src.getComponentIfExists<position_component>()) { dest.addComponent<position_component>(*c); }
	if (auto* c = src.getComponentIfExists<position_rotation_component>()) { dest.addComponent<position_rotation_component>(*c); }
	if (auto* c = src.getComponentIfExists<dynamic_transform_component>()) { dest.addComponent<dynamic_transform_component>(*c); }

#ifndef PHYSICS_ONLY
	if (auto* c = src.getComponentIfExists<point_light_component>()) { dest.addComponent<point_light_component>(*c); }
	if (auto* c = src.getComponentIfExists<spot_light_component>()) { dest.addComponent<spot_light_component>(*c); }
#endif

	/*
	TODO:
		- Animation
		- Raster
		- Raytrace
	*/

	return dest;
}

void game_scene::deleteEntity(scene_entity e)
{
	registry.destroy(e.handle);
}
