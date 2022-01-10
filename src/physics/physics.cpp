#include "pch.h"
#include "physics.h"
#include "collision_broad.h"
#include "collision_narrow.h"
#include "geometry/mesh_builder.h"
#include "core/cpu_profiling.h"

#include <unordered_set>

physics_settings physicsSettings = {};
std::function<void(const collision_event&)> collisionCallback;
static std::vector<bounding_hull_geometry> boundingHullGeometries;

struct constraint_context
{
	std::vector<constraint_edge> constraintEdges;
	uint16 firstFreeConstraintEdge = INVALID_CONSTRAINT_EDGE; // Free-list in constraintEdges array.


	constraint_edge& getFreeConstraintEdge()
	{
		if (firstFreeConstraintEdge == INVALID_CONSTRAINT_EDGE)
		{
			firstFreeConstraintEdge = (uint16)constraintEdges.size();
			constraintEdges.push_back(constraint_edge{ entt::null, constraint_type_none, INVALID_CONSTRAINT_EDGE, INVALID_CONSTRAINT_EDGE });
		}

		constraint_edge& edge = constraintEdges[firstFreeConstraintEdge];

		firstFreeConstraintEdge = edge.nextConstraintEdge;

		// Edge will be initialized by caller.

		return edge;
	}

	void freeConstraintEdge(constraint_edge& edge)
	{
		uint16 index = (uint16)(&edge - constraintEdges.data());
		edge.nextConstraintEdge = firstFreeConstraintEdge;
		firstFreeConstraintEdge = index;
	}
};

struct force_field_global_state
{
	vec3 force;
};

#ifndef PHYSICS_ONLY
// This is a bit dirty. PHYSICS_ONLY is defined when building the learning DLL, where we don't need bounding hulls.

#include "core/assimp.h"

uint32 allocateBoundingHullGeometry(const std::string& meshFilepath)
{
	Assimp::Importer importer;
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_TEXCOORDS | aiComponent_NORMALS | aiComponent_TANGENTS_AND_BITANGENTS | aiComponent_COLORS);

	const aiScene* scene = loadAssimpSceneFile(meshFilepath, importer);

	if (!scene)
	{
		return INVALID_BOUNDING_HULL_INDEX;
	}

	mesh_builder builder(mesh_creation_flags_with_positions);

	assert(scene->mNumMeshes == 1);
	builder.pushAssimpMesh(scene->mMeshes[0], 1.f);


	uint32 index = (uint32)boundingHullGeometries.size();
	boundingHullGeometries.push_back(bounding_hull_geometry::fromMesh(
		builder.getPositions(),
		builder.getNumVertices(),
		(indexed_triangle16*)builder.getTriangles(),
		builder.getNumTriangles()));
	return index;
}
#endif

static void addConstraintEdge(scene_entity& e, constraint_entity_reference_component& constraintEntityReference, entt::entity constraintEntity, constraint_type type)
{
	if (!e.hasComponent<physics_reference_component>())
	{
		e.addComponent<physics_reference_component>();
	}

	physics_reference_component& reference = e.getComponent<physics_reference_component>();

	constraint_context& context = e.registry->ctx_or_set<constraint_context>();

	constraint_edge& edge = context.getFreeConstraintEdge();
	uint16 edgeIndex = (uint16)(&edge - context.constraintEdges.data());

	edge.constraintEntity = constraintEntity;
	edge.type = type;
	edge.prevConstraintEdge = INVALID_CONSTRAINT_EDGE;
	edge.nextConstraintEdge = reference.firstConstraintEdge;

	if (reference.firstConstraintEdge != INVALID_CONSTRAINT_EDGE)
	{
		context.constraintEdges[reference.firstConstraintEdge].prevConstraintEdge = edgeIndex;
	}

	reference.firstConstraintEdge = edgeIndex;

	if (constraintEntityReference.edgeA == INVALID_CONSTRAINT_EDGE)
	{
		constraintEntityReference.edgeA = edgeIndex;
		constraintEntityReference.entityA = e.handle;
	}
	else
	{
		assert(constraintEntityReference.edgeB == INVALID_CONSTRAINT_EDGE);
		constraintEntityReference.edgeB = edgeIndex;
		constraintEntityReference.entityB = e.handle;
	}
}

distance_constraint_handle addDistanceConstraintFromLocalPoints(scene_entity& a, scene_entity& b, vec3 localAnchorA, vec3 localAnchorB, float distance)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	distance_constraint& constraint = registry.emplace<distance_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	constraint.localAnchorA = localAnchorA;
	constraint.localAnchorB = localAnchorB;
	constraint.globalLength = distance;

	addConstraintEdge(a, ref, constraintEntity, constraint_type_distance);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_distance);

	return { constraintEntity };
}

distance_constraint_handle addDistanceConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchorA, vec3 globalAnchorB)
{
	assert(a.registry == b.registry);

	vec3 localAnchorA = inverseTransformPosition(a.getComponent<transform_component>(), globalAnchorA);
	vec3 localAnchorB = inverseTransformPosition(b.getComponent<transform_component>(), globalAnchorB);
	float distance = length(globalAnchorA - globalAnchorB);

	return addDistanceConstraintFromLocalPoints(a, b, localAnchorA, localAnchorB, distance);
}

ball_constraint_handle addBallConstraintFromLocalPoints(scene_entity& a, scene_entity& b, vec3 localAnchorA, vec3 localAnchorB)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	ball_constraint& constraint = registry.emplace<ball_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	constraint.localAnchorA = localAnchorA;
	constraint.localAnchorB = localAnchorB;

	addConstraintEdge(a, ref, constraintEntity, constraint_type_ball);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_ball);

	return { constraintEntity };
}

ball_constraint_handle addBallConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchor)
{
	assert(a.registry == b.registry);

	vec3 localAnchorA = inverseTransformPosition(a.getComponent<transform_component>(), globalAnchor);
	vec3 localAnchorB = inverseTransformPosition(b.getComponent<transform_component>(), globalAnchor);

	return addBallConstraintFromLocalPoints(a, b, localAnchorA, localAnchorB);
}

fixed_constraint_handle addFixedConstraintFromLocalPoints(scene_entity& a, scene_entity& b, vec3 localAnchorA, vec3 localAnchorB)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	fixed_constraint& constraint = registry.emplace<fixed_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	constraint.localAnchorA = localAnchorA;
	constraint.localAnchorB = localAnchorB;

	addConstraintEdge(a, ref, constraintEntity, constraint_type_fixed);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_fixed);

	return { constraintEntity };
}

fixed_constraint_handle addFixedConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchor)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	fixed_constraint& constraint = registry.emplace<fixed_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	const transform_component& transformA = a.getComponent<transform_component>();
	const transform_component& transformB = b.getComponent<transform_component>();

	constraint.localAnchorA = inverseTransformPosition(transformA, globalAnchor);
	constraint.localAnchorB = inverseTransformPosition(transformB, globalAnchor);

	constraint.initialInvRotationDifference = conjugate(transformB.rotation) * transformA.rotation;

	addConstraintEdge(a, ref, constraintEntity, constraint_type_fixed);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_fixed);

	return { constraintEntity };
}

hinge_constraint_handle addHingeConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchor, vec3 globalHingeAxis,
	float minLimit, float maxLimit)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	hinge_constraint& constraint = registry.emplace<hinge_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	const transform_component& transformA = a.getComponent<transform_component>();
	const transform_component& transformB = b.getComponent<transform_component>();

	constraint.localAnchorA = inverseTransformPosition(transformA, globalAnchor);
	constraint.localAnchorB = inverseTransformPosition(transformB, globalAnchor);
	constraint.localHingeAxisA = inverseTransformDirection(transformA, globalHingeAxis);
	constraint.localHingeAxisB = inverseTransformDirection(transformB, globalHingeAxis);

	getTangents(constraint.localHingeAxisA, constraint.localHingeTangentA, constraint.localHingeBitangentA);
	constraint.localHingeTangentB = conjugate(transformB.rotation) * (transformA.rotation * constraint.localHingeTangentA);

	// Limits.
	constraint.minRotationLimit = minLimit;
	constraint.maxRotationLimit = maxLimit;

	// Motor.
	constraint.motorType = constraint_velocity_motor;
	constraint.motorVelocity = 0.f;
	constraint.maxMotorTorque = -1.f; // Disabled by default.

	addConstraintEdge(a, ref, constraintEntity, constraint_type_hinge);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_hinge);

	return { constraintEntity };
}

cone_twist_constraint_handle addConeTwistConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchor, vec3 globalAxis, 
	float swingLimit, float twistLimit)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	cone_twist_constraint& constraint = registry.emplace<cone_twist_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	const transform_component& transformA = a.getComponent<transform_component>();
	const transform_component& transformB = b.getComponent<transform_component>();

	constraint.localAnchorA = inverseTransformPosition(transformA, globalAnchor);
	constraint.localAnchorB = inverseTransformPosition(transformB, globalAnchor);

	// Limits.
	constraint.swingLimit = swingLimit;
	constraint.twistLimit = twistLimit;
	constraint.localLimitAxisA = inverseTransformDirection(transformA, globalAxis);
	constraint.localLimitAxisB = inverseTransformDirection(transformB, globalAxis);

	getTangents(constraint.localLimitAxisA, constraint.localLimitTangentA, constraint.localLimitBitangentA);
	constraint.localLimitTangentB = conjugate(transformB.rotation) * (transformA.rotation * constraint.localLimitTangentA);

	// Motor.
	constraint.swingMotorType = constraint_velocity_motor;
	constraint.swingMotorVelocity = 0.f;
	constraint.maxSwingMotorTorque = -1.f; // Disabled by default.
	constraint.swingMotorAxis = 0.f;

	constraint.twistMotorType = constraint_velocity_motor;
	constraint.twistMotorVelocity = 0.f;
	constraint.maxTwistMotorTorque = -1.f; // Disabled by default.

	addConstraintEdge(a, ref, constraintEntity, constraint_type_cone_twist);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_cone_twist);

	return { constraintEntity };
}

slider_constraint_handle addSliderConstraintFromGlobalPoints(scene_entity& a, scene_entity& b, vec3 globalAnchor, vec3 globalAxis, float minLimit, float maxLimit)
{
	assert(a.registry == b.registry);

	entt::registry& registry = *a.registry;
	entt::entity constraintEntity = registry.create();
	slider_constraint& constraint = registry.emplace<slider_constraint>(constraintEntity);
	constraint_entity_reference_component& ref = registry.emplace<constraint_entity_reference_component>(constraintEntity);

	const transform_component& transformA = a.getComponent<transform_component>();
	const transform_component& transformB = b.getComponent<transform_component>();

	constraint.localAnchorA = inverseTransformPosition(transformA, globalAnchor);
	constraint.localAnchorB = inverseTransformPosition(transformB, globalAnchor);

	constraint.localAxisA = inverseTransformDirection(transformA, globalAxis);
	constraint.initialInvRotationDifference = conjugate(transformB.rotation) * transformA.rotation;

	constraint.negDistanceLimit = minLimit;
	constraint.posDistanceLimit = maxLimit;

	constraint.motorType = constraint_velocity_motor;
	constraint.motorVelocity = 0.f;
	constraint.maxMotorForce = -1.f; // Disabled by default.

	addConstraintEdge(a, ref, constraintEntity, constraint_type_slider);
	addConstraintEdge(b, ref, constraintEntity, constraint_type_slider);

	return { constraintEntity };
}

distance_constraint& getConstraint(game_scene& scene, distance_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<distance_constraint>();
}

ball_constraint& getConstraint(game_scene& scene, ball_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<ball_constraint>();
}

fixed_constraint& getConstraint(game_scene& scene, fixed_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<fixed_constraint>();
}

hinge_constraint& getConstraint(game_scene& scene, hinge_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<hinge_constraint>();
}

cone_twist_constraint& getConstraint(game_scene& scene, cone_twist_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<cone_twist_constraint>();
}

slider_constraint& getConstraint(game_scene& scene, slider_constraint_handle handle)
{
	return scene_entity{ handle.entity, scene }.getComponent<slider_constraint>();
}

void deleteAllConstraints(game_scene& scene)
{
	scene.deleteAllComponents<distance_constraint>();
	scene.deleteAllComponents<ball_constraint>();
	scene.deleteAllComponents<fixed_constraint>();
	scene.deleteAllComponents<hinge_constraint>();
	scene.deleteAllComponents<cone_twist_constraint>();
	scene.deleteAllComponents<slider_constraint>();
}

static void removeConstraintEdge(scene_entity entity, constraint_edge& edge, constraint_context& context)
{
	if (edge.prevConstraintEdge != INVALID_CONSTRAINT_EDGE)
	{
		context.constraintEdges[edge.prevConstraintEdge].nextConstraintEdge = edge.nextConstraintEdge;
	}
	else
	{
		physics_reference_component& ref = entity.getComponent<physics_reference_component>();
		ref.firstConstraintEdge = edge.nextConstraintEdge;
	}

	if (edge.nextConstraintEdge != INVALID_CONSTRAINT_EDGE)
	{
		context.constraintEdges[edge.nextConstraintEdge].prevConstraintEdge = edge.prevConstraintEdge;
	}

	context.freeConstraintEdge(edge);
}

static void deleteConstraint(entt::registry* registry, entt::entity constraintEntityHandle)
{
	scene_entity constraintEntity = { constraintEntityHandle, registry };
	constraint_entity_reference_component& constraint = constraintEntity.getComponent<constraint_entity_reference_component>();

	scene_entity parentEntityA = { constraint.entityA, registry };
	scene_entity parentEntityB = { constraint.entityB, registry };

	constraint_context& context = registry->ctx<constraint_context>();

	constraint_edge& edgeA = context.constraintEdges[constraint.edgeA];
	constraint_edge& edgeB = context.constraintEdges[constraint.edgeB];

	removeConstraintEdge(parentEntityA, edgeA, context);
	removeConstraintEdge(parentEntityB, edgeB, context);

	registry->destroy(constraintEntity.handle);
}

void deleteConstraint(game_scene& scene, distance_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteConstraint(game_scene& scene, ball_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteConstraint(game_scene& scene, fixed_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteConstraint(game_scene& scene, hinge_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteConstraint(game_scene& scene, cone_twist_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteConstraint(game_scene& scene, slider_constraint_handle handle)
{
	deleteConstraint(&scene.registry, handle.entity);
}

void deleteAllConstraintsFromEntity(scene_entity& entity)
{
	entt::registry* registry = entity.registry;
	if (constraint_context* context = registry->try_ctx<constraint_context>())
	{
		if (physics_reference_component* ref = entity.getComponentIfExists<physics_reference_component>())
		{
			for (auto edgeIndex = ref->firstConstraintEdge; edgeIndex != INVALID_CONSTRAINT_EDGE; )
			{
				constraint_edge& edge = context->constraintEdges[edgeIndex];
				edgeIndex = edge.nextConstraintEdge;

				deleteConstraint(registry, edge.constraintEntity);
			}
		}
	}
}

constraint_entity_iterator::iterator& constraint_entity_iterator::iterator::operator++()
{
	constraint_context& context = registry->ctx<constraint_context>();
	constraintEdgeIndex = context.constraintEdges[constraintEdgeIndex].nextConstraintEdge;
	return *this;
}

std::pair<scene_entity, constraint_type> constraint_entity_iterator::iterator::operator*()
{
	constraint_context& context = registry->ctx<constraint_context>();
	return { { context.constraintEdges[constraintEdgeIndex].constraintEntity, registry }, context.constraintEdges[constraintEdgeIndex].type };
}


void testPhysicsInteraction(game_scene& scene, ray r)
{
	float minT = FLT_MAX;
	rigid_body_component* minRB = 0;
	vec3 force;
	vec3 torque;

	for (auto [entityHandle, collider] : scene.view<collider_component>().each())
	{
		scene_entity rbEntity = { collider.parentEntity, scene };
		if (rigid_body_component* rb = rbEntity.getComponentIfExists<rigid_body_component>())
		{
			transform_component& transform = rbEntity.getComponent<transform_component>();

			ray localR = { inverseTransformPosition(transform, r.origin), inverseTransformDirection(transform, r.direction) };
			float t;
			bool hit = false;

			switch (collider.type)
			{
				case collider_type_sphere:
				{
					hit = localR.intersectSphere(collider.sphere, t);
				} break;

				case collider_type_capsule:
				{
					hit = localR.intersectCapsule(collider.capsule, t);
				} break;

				case collider_type_cylinder:
				{
					hit = localR.intersectCylinder(collider.cylinder, t);
				} break;

				case collider_type_aabb:
				{
					hit = localR.intersectAABB(collider.aabb, t);
				} break;

				case collider_type_obb:
				{
					hit = localR.intersectOBB(collider.obb, t);
				} break;

				case collider_type_hull:
				{
					hit = localR.intersectHull(collider.hull, boundingHullGeometries[collider.hull.geometryIndex], t);
				} break;
			}

			if (hit && t < minT)
			{
				minT = t;
				minRB = rb;

				vec3 localHit = localR.origin + t * localR.direction;
				vec3 globalHit = transformPosition(transform, localHit);

				vec3 cogPosition = rb->getGlobalCOGPosition(transform);

				force = r.direction * physicsSettings.testForce;
				torque = cross(globalHit - cogPosition, force);
			}
		}
	}

	if (minRB)
	{
		minRB->torqueAccumulator += torque;
		minRB->forceAccumulator += force;
	}
}

static void getWorldSpaceColliders(game_scene& scene, bounding_box* outWorldspaceAABBs, collider_union* outWorldSpaceColliders, uint16 dummyRigidBodyIndex)
{
	CPU_PROFILE_BLOCK("Get world space colliders");

	uint32 pushIndex = 0;

	for (auto [entityHandle, collider] : scene.view<collider_component>().each())
	{
		bounding_box& bb = outWorldspaceAABBs[pushIndex];
		collider_union& col = outWorldSpaceColliders[pushIndex];
		++pushIndex;

		scene_entity entity = { collider.parentEntity, scene };
		const transform_component& transform = entity.hasComponent<transform_component>() ? entity.getComponent<transform_component>() : trs::identity;

		col.type = collider.type;
		col.properties = collider.properties;

		if (entity.hasComponent<rigid_body_component>())
		{
			col.objectIndex = (uint16)entity.getComponentIndex<rigid_body_component>();
			col.objectType = physics_object_type_rigid_body;
		}
		else if (entity.hasComponent<force_field_component>())
		{
			col.objectIndex = (uint16)entity.getComponentIndex<force_field_component>();
			col.objectType = physics_object_type_force_field;
		}
		else if (entity.hasComponent<trigger_component>())
		{
			col.objectIndex = (uint16)entity.getComponentIndex<trigger_component>();
			col.objectType = physics_object_type_trigger;
		}
		else
		{
			col.objectIndex = dummyRigidBodyIndex;
			col.objectType = physics_object_type_static_collider;
		}

		switch (collider.type)
		{
			case collider_type_sphere:
			{
				vec3 center = transform.position + transform.rotation * collider.sphere.center;
				bb = bounding_box::fromCenterRadius(center, collider.sphere.radius);
				col.sphere = { center, collider.sphere.radius };
			} break;

			case collider_type_capsule:
			{
				vec3 posA = transform.rotation * collider.capsule.positionA + transform.position;
				vec3 posB = transform.rotation * collider.capsule.positionB + transform.position;

				float radius = collider.capsule.radius;
				vec3 radius3(radius);

				bb = bounding_box::negativeInfinity();
				bb.grow(posA + radius3);
				bb.grow(posA - radius3);
				bb.grow(posB + radius3);
				bb.grow(posB - radius3);

				col.capsule = { posA, posB, radius };
			} break;

			case collider_type_cylinder:
			{
				vec3 posA = transform.rotation * collider.cylinder.positionA + transform.position;
				vec3 posB = transform.rotation * collider.cylinder.positionB + transform.position;
				float radius = collider.cylinder.radius;

				vec3 a = posB - posA;
				float aa = dot(a, a);

				float x = 1.f - a.x * a.x / aa;
				float y = 1.f - a.y * a.y / aa;
				float z = 1.f - a.z * a.z / aa;
				x = sqrt(max(0.f, x));
				y = sqrt(max(0.f, y));
				z = sqrt(max(0.f, z));

				vec3 e = radius * vec3(x, y, z);

				bb = bounding_box::fromMinMax(minimum(posA - e, posB - e), maximum(posA + e, posB + e));

				col.cylinder = { posA, posB, radius };
			} break;

			case collider_type_aabb:
			{
				bb = collider.aabb.transformToAABB(transform.rotation, transform.position);
				if (transform.rotation == quat::identity)
				{
					col.aabb = bb;
				}
				else
				{
					col.type = collider_type_obb;
					col.obb = collider.aabb.transformToOBB(transform.rotation, transform.position);
				}
			} break;

			case collider_type_obb:
			{
				bb = collider.obb.transformToAABB(transform.rotation, transform.position);
				col.obb = collider.obb.transformToOBB(transform.rotation, transform.position);
			} break;

			case collider_type_hull:
			{
				const bounding_hull_geometry& geometry = boundingHullGeometries[collider.hull.geometryIndex];

				quat rotation = transform.rotation * collider.hull.rotation;
				vec3 position = transform.rotation * collider.hull.position + transform.position;

				bb = geometry.aabb.transformToAABB(rotation, position);
				col.hull.rotation = rotation;
				col.hull.position = position;
				col.hull.geometryPtr = &geometry;
			} break;
		}
	}
}

// Returns the accumulated force from all global force fields and writes localized forces (from force fields with colliders) in outLocalizedForceFields.
static vec3 getForceFieldStates(game_scene& scene, force_field_global_state* outLocalForceFields)
{
	vec3 globalForceField(0.f);

	for (auto [entityHandle, forceField] : scene.view<force_field_component>().each())
	{
		scene_entity entity = { entityHandle, scene };

		vec3 force = forceField.force; 
		if (transform_component* transform = entity.getComponentIfExists<transform_component>())
		{
			force = transform->rotation * force;
		}

		if (entity.hasComponent<collider_component>())
		{
			// Localized force field.
			uint16 index = (uint16)entity.getComponentIndex<force_field_component>();
			outLocalForceFields[index].force = force;
		}
		else
		{
			// Global force field.
			globalForceField += force;
		}
	}

	return globalForceField;
}

template <typename constraint_t>
static void getConstraintBodyPairs(game_scene& scene, constraint_body_pair* bodyPairs)
{
	uint32 i = scene.numberOfComponentsOfType<constraint_t>() - 1;
	for (auto [entityHandle, _] : scene.view<constraint_t>().each())
	{
		scene_entity entity = { entityHandle, scene };
		constraint_entity_reference_component& reference = entity.getComponent<constraint_entity_reference_component>();

		constraint_body_pair& pair = bodyPairs[i--];

		scene_entity rbAEntity = { reference.entityA, scene };
		scene_entity rbBEntity = { reference.entityB, scene };
		pair.rbA = (uint16)rbAEntity.getComponentIndex<rigid_body_component>();
		pair.rbB = (uint16)rbBEntity.getComponentIndex<rigid_body_component>();
	}
}

#if 0
#define VALIDATE1(line, prefix, value) if (!isfinite(value)) { bool nan = isnan(value); std::cout << prefix << "(" << line << "): " << #value << " is " << (nan ? "NaN" : "Inf") << '\n'; }
#define VALIDATE3(line, prefix, value) VALIDATE1(line, prefix, value.x); VALIDATE1(line, prefix, value.y); VALIDATE1(line, prefix, value.z);
#define VALIDATE4(line, prefix, value) VALIDATE1(line, prefix, value.x); VALIDATE1(line, prefix, value.y); VALIDATE1(line, prefix, value.z); VALIDATE1(line, prefix, value.w);

void validate(uint32 line, collider_union* colliders, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		collider_union& c = colliders[i];
		switch (c.type)
		{
			case collider_type_sphere: VALIDATE3(line, "Collider", c.sphere.center); VALIDATE1(line, "Collider", c.sphere.radius); break;
			case collider_type_capsule: VALIDATE3(line, "Collider", c.capsule.positionA); VALIDATE3(line, "Collider", c.capsule.positionB); VALIDATE1(line, "Collider", c.capsule.radius); break;
			case collider_type_aabb: VALIDATE3(line, "Collider", c.aabb.minCorner); VALIDATE3(line, "Collider", c.aabb.maxCorner); break;
			case collider_type_obb: VALIDATE3(line, "Collider", c.obb.center); VALIDATE3(line, "Collider", c.obb.radius); VALIDATE4(line, "Collider", c.obb.rotation); break;
			case collider_type_hull: break;
		}
	}
}
void validate(uint32 line, bounding_box* colliders, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		bounding_box& c = colliders[i];
		VALIDATE3(line, "World space BB", c.minCorner);
		VALIDATE3(line, "World space BB", c.maxCorner);
	}
}
void validate(uint32 line, rigid_body_global_state* rbs, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		rigid_body_global_state& r = rbs[i];
		VALIDATE4(line, "RB update", r.rotation);
		VALIDATE3(line, "RB update", r.localCOGPosition);
		VALIDATE3(line, "RB update", r.position);
		VALIDATE3(line, "RB update", r.invInertia.col0);
		VALIDATE3(line, "RB update", r.invInertia.col1);
		VALIDATE3(line, "RB update", r.invInertia.col2);
		VALIDATE1(line, "RB update", r.invMass);
		VALIDATE3(line, "RB update", r.linearVelocity);
		VALIDATE3(line, "RB update", r.angularVelocity);
	}
}
void validate(uint32 line, collision_contact* contacts, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		collision_contact& c = contacts[i];
		VALIDATE3(line, "Contact", c.point);
		VALIDATE1(line, "Contact", c.penetrationDepth);
		VALIDATE3(line, "Contact", c.normal);
	}
}
void validate(uint32 line, collision_constraint* constraints, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		collision_constraint& c = constraints[i];
		VALIDATE3(line, "Collision", c.relGlobalAnchorA);
		VALIDATE3(line, "Collision", c.relGlobalAnchorB);
		VALIDATE3(line, "Collision", c.tangent);
		VALIDATE3(line, "Collision", c.tangentImpulseToAngularVelocityA);
		VALIDATE3(line, "Collision", c.tangentImpulseToAngularVelocityB);
		VALIDATE3(line, "Collision", c.normalImpulseToAngularVelocityA);
		VALIDATE3(line, "Collision", c.normalImpulseToAngularVelocityB);
		VALIDATE1(line, "Collision", c.impulseInNormalDir);
		VALIDATE1(line, "Collision", c.impulseInTangentDir);
		VALIDATE1(line, "Collision", c.effectiveMassInNormalDir);
		VALIDATE1(line, "Collision", c.effectiveMassInTangentDir);
		VALIDATE1(line, "Collision", c.bias);
	}
}
void validate(uint32 line, simd_collision_constraint_batch* constraints, uint32 count)
{
	for (uint32 i = 0; i < count; ++i)
	{
		simd_collision_constraint_batch& c = constraints[i];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			VALIDATE1(line, "Collision", c.relGlobalAnchorA[0][j]);
			VALIDATE1(line, "Collision", c.relGlobalAnchorA[1][j]);
			VALIDATE1(line, "Collision", c.relGlobalAnchorA[2][j]);
			VALIDATE1(line, "Collision", c.relGlobalAnchorB[0][j]);
			VALIDATE1(line, "Collision", c.relGlobalAnchorB[1][j]);
			VALIDATE1(line, "Collision", c.relGlobalAnchorB[2][j]);
			VALIDATE1(line, "Collision", c.normal[0][j]);
			VALIDATE1(line, "Collision", c.normal[1][j]);
			VALIDATE1(line, "Collision", c.normal[2][j]);
			VALIDATE1(line, "Collision", c.tangent[0][j]);
			VALIDATE1(line, "Collision", c.tangent[1][j]);
			VALIDATE1(line, "Collision", c.tangent[2][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityA[0][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityA[1][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityA[2][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityA[0][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityA[1][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityA[2][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityB[0][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityB[1][j]);
			VALIDATE1(line, "Collision", c.normalImpulseToAngularVelocityB[2][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityB[0][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityB[1][j]);
			VALIDATE1(line, "Collision", c.tangentImpulseToAngularVelocityB[2][j]);
			VALIDATE1(line, "Collision", c.effectiveMassInNormalDir[j]);
			VALIDATE1(line, "Collision", c.effectiveMassInTangentDir[j]);
			VALIDATE1(line, "Collision", c.friction[j]);
			VALIDATE1(line, "Collision", c.impulseInNormalDir[j]);
			VALIDATE1(line, "Collision", c.impulseInTangentDir[j]);
			VALIDATE1(line, "Collision", c.bias[j]);
		}
	}
}

#define VALIDATE(arr, count) validate(__LINE__, arr, count)

#else
#define VALIDATE(...)
#endif


struct entity_pair
{
	entt::entity a;
	entt::entity b;
};

namespace std
{
	template<> struct hash<entity_pair> { size_t operator()(entity_pair x) const { return std::hash<uint64>()(((uint64)x.a << 32) | ((uint64)x.b)); } };
}

static bool operator==(entity_pair a, entity_pair b) { return a.a == b.a && a.b == b.b; }

static std::unordered_set<entity_pair> triggerOverlaps[2];
static std::unordered_set<entity_pair> objectContacts[2];
static uint32 historyIndex = 0;


void physicsStep(game_scene& scene, memory_arena& arena, float dt)
{
	CPU_PROFILE_BLOCK("Physics step");

	if (physicsSettings.globalTimeScale <= 0.f)
	{
		return;
	}

	dt = min(dt, 1.f / 30.f);
	dt *= physicsSettings.globalTimeScale;

	uint32 numRigidBodies = scene.numberOfComponentsOfType<rigid_body_component>();
	uint32 numCloths = scene.numberOfComponentsOfType<cloth_component>();
	if (numRigidBodies == 0 && numCloths == 0)
	{
		return;
	}

	uint32 numForceFields = scene.numberOfComponentsOfType<force_field_component>();
	uint32 numTriggers = scene.numberOfComponentsOfType<trigger_component>();
	uint32 numColliders = scene.numberOfComponentsOfType<collider_component>();

	uint32 numDistanceConstraints = scene.numberOfComponentsOfType<distance_constraint>();
	uint32 numBallConstraints = scene.numberOfComponentsOfType<ball_constraint>();
	uint32 numFixedConstraints = scene.numberOfComponentsOfType<fixed_constraint>();
	uint32 numHingeConstraints = scene.numberOfComponentsOfType<hinge_constraint>();
	uint32 numConeTwistConstraints = scene.numberOfComponentsOfType<cone_twist_constraint>();
	uint32 numSliderConstraints = scene.numberOfComponentsOfType<slider_constraint>();
	uint32 numConstraints = numDistanceConstraints + numBallConstraints + numFixedConstraints + numHingeConstraints + numConeTwistConstraints + numSliderConstraints;



	memory_marker marker = arena.getMarker();

	rigid_body_global_state* rbGlobal = arena.allocate<rigid_body_global_state>(numRigidBodies + 1); // Reserve one slot for dummy.
	force_field_global_state* ffGlobal = arena.allocate<force_field_global_state>(numForceFields);
	bounding_box* worldSpaceAABBs = arena.allocate<bounding_box>(numColliders);
	collider_union* worldSpaceColliders = arena.allocate<collider_union>(numColliders);

	collider_pair* collisionPairs = arena.allocate<collider_pair>(numColliders * numColliders); // Conservative estimate.

	uint32 dummyRigidBodyIndex = numRigidBodies;

	// Collision detection.
	getWorldSpaceColliders(scene, worldSpaceAABBs, worldSpaceColliders, dummyRigidBodyIndex);
	VALIDATE(worldSpaceColliders, numColliders);
	VALIDATE(worldSpaceAABBs, numColliders);

	uint32 numBroadphaseOverlaps = broadphase(scene, worldSpaceAABBs, arena, collisionPairs);

	non_collision_interaction* nonCollisionInteractions = arena.allocate<non_collision_interaction>(numBroadphaseOverlaps);
	collision_contact* contacts = arena.allocate<collision_contact>(numBroadphaseOverlaps * 4); // Each collision can have up to 4 contact points.
	constraint_body_pair* allConstraintBodyPairs = arena.allocate<constraint_body_pair>(
		numConstraints + numBroadphaseOverlaps * 4);

	constraint_body_pair* collisionBodyPairs = allConstraintBodyPairs + numConstraints;

	narrowphase_result narrowPhaseResult = narrowphase(worldSpaceColliders, collisionPairs, numBroadphaseOverlaps, contacts, collisionBodyPairs, nonCollisionInteractions);
	VALIDATE(contacts, narrowPhaseResult.numContacts);


	auto& prevFrameObjectContacts = objectContacts[historyIndex];
	auto& thisFrameObjectContacts = objectContacts[1 - historyIndex];
	thisFrameObjectContacts.clear();

	for (uint32 i = 0; i < narrowPhaseResult.numCollisions; ++i)
	{
		collider_pair pair = collisionPairs[i];

		scene_entity colliderAEntity = scene.getEntityFromComponentAtIndex<collider_component>(pair.colliderA);
		scene_entity colliderBEntity = scene.getEntityFromComponentAtIndex<collider_component>(pair.colliderB);

		entity_pair contact = { colliderAEntity.handle, colliderBEntity.handle };
		thisFrameObjectContacts.insert(contact);
	}


	vec3 globalForceField = getForceFieldStates(scene, ffGlobal);

	auto& prevFrameTriggerOverlaps = triggerOverlaps[historyIndex];
	auto& thisFrameTriggerOverlaps = triggerOverlaps[1 - historyIndex];
	thisFrameTriggerOverlaps.clear();
	
	// Handle non-collision interactions (triggers, force fields etc).
	for (uint32 i = 0; i < narrowPhaseResult.numNonCollisionInteractions; ++i)
	{
		non_collision_interaction interaction = nonCollisionInteractions[i];
		rigid_body_component& rb = scene.getComponentAtIndex<rigid_body_component>(interaction.rigidBodyIndex);

		if (interaction.otherType == physics_object_type_force_field)
		{
			const force_field_global_state& ff = ffGlobal[interaction.otherIndex];
			rb.forceAccumulator += ff.force;
		}
		else if (interaction.otherType == physics_object_type_trigger)
		{
			scene_entity triggerEntity = scene.getEntityFromComponentAtIndex<trigger_component>(interaction.otherIndex);
			scene_entity rbEntity = scene.getEntityFromComponent(rb);

			entity_pair overlap = { triggerEntity.handle, rbEntity.handle };
			thisFrameTriggerOverlaps.insert(overlap);
		}
	}

	CPU_PROFILE_STAT("Num rigid bodies", numRigidBodies);
	CPU_PROFILE_STAT("Num colliders", numColliders);
	CPU_PROFILE_STAT("Num broadphase overlaps", numBroadphaseOverlaps);
	CPU_PROFILE_STAT("Num narrowphase overlaps", narrowPhaseResult.numCollisions);
	CPU_PROFILE_STAT("Num narrowphase contacts", narrowPhaseResult.numContacts);


	//  Apply global forces (including gravity) and air drag and integrate forces.
	{
		CPU_PROFILE_BLOCK("Integrate rigid body forces");

		uint32 rbIndex = numRigidBodies - 1; // EnTT iterates back to front.
		for (auto [entityHandle, rb, transform] : scene.group<rigid_body_component, transform_component>().each())
		{
			rigid_body_global_state& global = rbGlobal[rbIndex--];
			rb.forceAccumulator += globalForceField;
			rb.applyGravityAndIntegrateForces(global, transform, dt);
		}
	}

	// Kinematic rigid body. This is used in collision constraint solving, when a collider has no rigid body.
	memset(&rbGlobal[dummyRigidBodyIndex], 0, sizeof(rigid_body_global_state));

	VALIDATE(rbGlobal, numRigidBodies);

	// Collect constraints.
	uint32 numContacts = narrowPhaseResult.numContacts;

	distance_constraint* distanceConstraints = scene.raw<distance_constraint>();
	ball_constraint* ballConstraints = scene.raw<ball_constraint>();
	fixed_constraint* fixedConstraints = scene.raw<fixed_constraint>();
	hinge_constraint* hingeConstraints = scene.raw<hinge_constraint>();
	cone_twist_constraint* coneTwistConstraints = scene.raw<cone_twist_constraint>();
	slider_constraint* sliderConstraints = scene.raw<slider_constraint>();

	constraint_body_pair* distanceConstraintBodyPairs = allConstraintBodyPairs + 0;
	constraint_body_pair* ballConstraintBodyPairs = distanceConstraintBodyPairs + numDistanceConstraints;
	constraint_body_pair* fixedConstraintBodyPairs = ballConstraintBodyPairs + numBallConstraints;
	constraint_body_pair* hingeConstraintBodyPairs = fixedConstraintBodyPairs + numFixedConstraints;
	constraint_body_pair* coneTwistConstraintBodyPairs = hingeConstraintBodyPairs + numHingeConstraints;
	constraint_body_pair* sliderConstraintBodyPairs = coneTwistConstraintBodyPairs + numConeTwistConstraints;

	getConstraintBodyPairs<distance_constraint>(scene, distanceConstraintBodyPairs);
	getConstraintBodyPairs<ball_constraint>(scene, ballConstraintBodyPairs);
	getConstraintBodyPairs<fixed_constraint>(scene, fixedConstraintBodyPairs);
	getConstraintBodyPairs<hinge_constraint>(scene, hingeConstraintBodyPairs);
	getConstraintBodyPairs<cone_twist_constraint>(scene, coneTwistConstraintBodyPairs);
	getConstraintBodyPairs<slider_constraint>(scene, sliderConstraintBodyPairs);


	// Solve constraints.
	constraint_solver constraintSolver;
	constraintSolver.initialize(arena, rbGlobal,
		distanceConstraints, distanceConstraintBodyPairs, numDistanceConstraints,
		ballConstraints, ballConstraintBodyPairs, numBallConstraints,
		fixedConstraints, fixedConstraintBodyPairs, numFixedConstraints,
		hingeConstraints, hingeConstraintBodyPairs, numHingeConstraints,
		coneTwistConstraints, coneTwistConstraintBodyPairs, numConeTwistConstraints,
		sliderConstraints, sliderConstraintBodyPairs, numSliderConstraints,
		contacts, collisionBodyPairs, numContacts,
		dummyRigidBodyIndex, physicsSettings.simd, dt);

	{
		CPU_PROFILE_BLOCK("Solve constraints");

		for (uint32 it = 0; it < physicsSettings.numRigidSolverIterations; ++it)
		{
			constraintSolver.solveOneIteration();
		}
	}


	// Integrate velocities.
	{
		CPU_PROFILE_BLOCK("Integrate rigid body velocities");

		uint32 rbIndex = numRigidBodies - 1; // EnTT iterates back to front.
		for (auto [entityHandle, rb, transform] : scene.group<rigid_body_component, transform_component>().each())
		{
			rigid_body_global_state& global = rbGlobal[rbIndex--];
			rb.integrateVelocity(global, transform, dt);
		}
	}

	VALIDATE(rbGlobal, numRigidBodies);

	// Cloth. This needs to get integrated with the rest of the system.

	for (auto [entityHandle, cloth] : scene.view<cloth_component>().each())
	{
		cloth.applyWindForce(globalForceField);
		cloth.simulate(physicsSettings.numClothVelocityIterations, physicsSettings.numClothPositionIterations, physicsSettings.numClothDriftIterations, dt);
	}

	arena.resetToMarker(marker);



	// Collision callbacks.
	if (collisionCallback)
	{
		for (entity_pair o : thisFrameObjectContacts)
		{
			// Didn't touch last frame -> fire collision start event.
			if (prevFrameObjectContacts.find(o) == prevFrameObjectContacts.end())
			{
				scene_entity colliderAEntity = { o.a, scene };
				scene_entity colliderBEntity = { o.b, scene };
				const collider_component& colliderA = colliderAEntity.getComponent<collider_component>();
				const collider_component& colliderB = colliderBEntity.getComponent<collider_component>();

				scene_entity entityA = { colliderA.parentEntity, scene };
				scene_entity entityB = { colliderB.parentEntity, scene };

				collisionCallback(collision_event{ entityA, colliderAEntity, colliderA, entityB, colliderBEntity, colliderB, collision_event_start });
			}
		}

		for (entity_pair o : prevFrameObjectContacts)
		{
			// Did touch last frame and don't this frame -> fire collision end event.
			if (thisFrameObjectContacts.find(o) == thisFrameObjectContacts.end())
			{
				scene_entity colliderAEntity = { o.a, scene };
				scene_entity colliderBEntity = { o.b, scene };
				if (scene.isEntityValid(colliderAEntity) && scene.isEntityValid(colliderBEntity))
				{
					const collider_component& colliderA = colliderAEntity.getComponent<collider_component>();
					const collider_component& colliderB = colliderBEntity.getComponent<collider_component>();

					scene_entity entityA = { colliderA.parentEntity, scene };
					scene_entity entityB = { colliderB.parentEntity, scene };

					collisionCallback(collision_event{ entityA, colliderAEntity, colliderA, entityB, colliderBEntity, colliderB, collision_event_end });
				}
			}
		}
	}


	// Trigger callbacks.
	for (entity_pair o : thisFrameTriggerOverlaps)
	{
		// Didn't overlap last frame -> fire enter event.
		if (prevFrameTriggerOverlaps.find(o) == prevFrameTriggerOverlaps.end())
		{
			scene_entity triggerEntity = { o.a, scene };
			scene_entity otherEntity = { o.b, scene };
			const trigger_component& triggerComp = triggerEntity.getComponent<trigger_component>();
			triggerComp.callback(trigger_event{ triggerEntity, otherEntity, trigger_event_enter });
		}
	}

	for (entity_pair o : prevFrameTriggerOverlaps)
	{
		// Did overlap last frame and don't this frame -> fire leave event.
		if (thisFrameTriggerOverlaps.find(o) == thisFrameTriggerOverlaps.end())
		{
			scene_entity triggerEntity = { o.a, scene };
			scene_entity otherEntity = { o.b, scene };
			if (scene.isEntityValid(triggerEntity) && scene.isEntityValid(otherEntity))
			{
				const trigger_component& triggerComp = triggerEntity.getComponent<trigger_component>();
				triggerComp.callback(trigger_event{ triggerEntity, otherEntity, trigger_event_leave });
			}
		}
	}

	historyIndex = 1 - historyIndex;
}

// This function returns the inertia tensors with respect to the center of gravity, so with a coordinate system centered at the COG.
physics_properties collider_union::calculatePhysicsProperties()
{
	physics_properties result;
	switch (type)
	{
		case collider_type_sphere:
		{
			result.mass = sphere.volume() * properties.density;
			result.cog = sphere.center;
			result.inertia = mat3::identity * (2.f / 5.f * result.mass * sphere.radius * sphere.radius);
		} break;

		case collider_type_capsule:
		{
			vec3 axis = capsule.positionA - capsule.positionB;
			if (axis.y < 0.f)
			{
				axis *= -1.f;
			}
			float height = length(axis);
			axis *= (1.f / height);

			quat rotation = rotateFromTo(vec3(0.f, 1.f, 0.f), axis);
			mat3 rot = quaternionToMat3(rotation);

			result.mass = capsule.volume() * properties.density;
			result.cog = (capsule.positionA + capsule.positionB) * 0.5f;
			
			// Inertia.
			float sqRadius = capsule.radius * capsule.radius;
			float sqRadiusPI = M_PI * sqRadius;

			float cylinderMass = properties.density * sqRadiusPI * height;
			float hemiSphereMass = properties.density * 2.f / 3.f * sqRadiusPI * capsule.radius;

			float sqCapsuleHeight = height * height;

			result.inertia.m11 = sqRadius * cylinderMass * 0.5f;
			result.inertia.m00 = result.inertia.m22 = result.inertia.m11 * 0.5f + cylinderMass * sqCapsuleHeight / 12.f;
			float temp0 = hemiSphereMass * 2.f * sqRadius / 5.f;
			result.inertia.m11 += temp0 * 2.f;
			float temp1 = height * 0.5f;
			float temp2 = temp0 + hemiSphereMass * (temp1 * temp1 + 3.f / 8.f * sqCapsuleHeight);
			result.inertia.m00 += temp2 * 2.f;
			result.inertia.m22 += temp2 * 2.f;
			result.inertia.m01 = result.inertia.m02 = result.inertia.m10 = result.inertia.m12 = result.inertia.m20 = result.inertia.m21 = 0.f;

			result.inertia = transpose(rot) * result.inertia * rot;
		} break;

		case collider_type_cylinder:
		{
			vec3 axis = cylinder.positionA - cylinder.positionB;
			if (axis.y < 0.f)
			{
				axis *= -1.f;
			}
			float height = length(axis);
			axis *= (1.f / height);

			quat rotation = rotateFromTo(vec3(0.f, 1.f, 0.f), axis);
			mat3 rot = quaternionToMat3(rotation);

			result.mass = cylinder.volume() * properties.density;
			result.cog = (cylinder.positionA + cylinder.positionB) * 0.5f;

			// Inertia.
			float sqRadius = cylinder.radius * cylinder.radius;
			float sqHeight = height * height;

			result.inertia.m11 = sqRadius * result.mass * 0.5f;
			result.inertia.m00 = result.inertia.m22 = 1.f / 12.f * result.mass * (3.f * sqRadius + sqHeight);
			result.inertia.m01 = result.inertia.m02 = result.inertia.m10 = result.inertia.m12 = result.inertia.m20 = result.inertia.m21 = 0.f;

			result.inertia = transpose(rot) * result.inertia * rot;
		} break;

		case collider_type_aabb:
		{
			result.mass = aabb.volume() * properties.density;
			result.cog = aabb.getCenter();

			vec3 diameter = aabb.getRadius() * 2.f;
			result.inertia = mat3::zero;
			result.inertia.m00 = 1.f / 12.f * result.mass * (diameter.y * diameter.y + diameter.z * diameter.z);
			result.inertia.m11 = 1.f / 12.f * result.mass * (diameter.x * diameter.x + diameter.z * diameter.z);
			result.inertia.m22 = 1.f / 12.f * result.mass * (diameter.x * diameter.x + diameter.y * diameter.y);
		} break;

		case collider_type_obb:
		{
			result.mass = obb.volume() * properties.density;
			result.cog = obb.center;

			vec3 diameter = obb.radius * 2.f;
			result.inertia = mat3::zero;
			result.inertia.m00 = 1.f / 12.f * result.mass * (diameter.y * diameter.y + diameter.z * diameter.z);
			result.inertia.m11 = 1.f / 12.f * result.mass * (diameter.x * diameter.x + diameter.z * diameter.z);
			result.inertia.m22 = 1.f / 12.f * result.mass * (diameter.x * diameter.x + diameter.y * diameter.y);

			mat3 rot = quaternionToMat3(obb.rotation);
			result.inertia = transpose(rot) * result.inertia * rot;
		} break;

		case collider_type_hull:
		{
			// http://number-none.com/blow/inertia/
			// http://number-none.com/blow/inertia/bb_inertia.doc

			const bounding_hull_geometry& geom = boundingHullGeometries[hull.geometryIndex];

			const float s60 = 1.f / 60.f;
			const float s120 = 1.f / 120.f;

			// Covariance-matrix of a tetrahedron with v0 = (0, 0, 0), v1 = (1, 0, 0), v2 = (0, 1, 0), v3 = (0, 0, 1).
			const mat3 Ccanonical(
				s60, s120, s120,
				s120, s60, s120,
				s120, s120, s60
			);

			uint32 numFaces = (uint32)geom.faces.size();

			float totalMass = 0.f;
			mat3 totalCovariance = mat3::zero;
			vec3 totalCOG(0.f);

			for (uint32 i = 0; i < numFaces; ++i)
			{
				auto& face = geom.faces[i];

				//vec3 w0 = 0.f;
				vec3 w1 = hull.position + hull.rotation * geom.vertices[face.a];
				vec3 w2 = hull.position + hull.rotation * geom.vertices[face.b];
				vec3 w3 = hull.position + hull.rotation * geom.vertices[face.c];

				mat3 A(
					w1.x, w2.x, w3.x,
					w1.y, w2.y, w3.y,
					w1.z, w2.z, w3.z
				);


				float detA = determinant(A);
				mat3 covariance = detA * A * Ccanonical * transpose(A);

				float volume = 1.f / 6.f * detA;
				float mass = volume;
				vec3 cog = (w1 + w2 + w3) * 0.25f;

				totalMass += mass;
				totalCovariance += covariance;
				totalCOG += cog * mass;
			}

			totalCOG /= totalMass;

			// This is actually different in the Blow-paper, but this one is correct.
			mat3 CprimeTotal = totalCovariance - totalMass * outerProduct(totalCOG, totalCOG);

			result.cog = totalCOG;
			result.mass = totalMass * properties.density;
			result.inertia = mat3::identity * trace(CprimeTotal) - CprimeTotal;
			result.inertia *= properties.density;
		} break;

		default:
		{
			assert(false);
		} break;
	}
	return result;
}
