#include "pch.h"
#include "cloth.h"
#include "physics.h"
#include "core/random.h"

cloth_component::cloth_component(float width, float height, uint32 gridSizeX, uint32 gridSizeY, float totalMass, float thickness, float damping, float gravityFactor)
{
	this->thickness = thickness;
	this->gravityFactor = gravityFactor;
	this->damping = damping;
	this->gridSizeX = gridSizeX;
	this->gridSizeY = gridSizeY;
	this->width = width;
	this->height = height;

	uint32 numParticles = gridSizeX * gridSizeY;

	float invMassPerParticle = numParticles / totalMass;

	positions.reserve(numParticles);
	prevPositions.reserve(numParticles);
	invMasses.reserve(numParticles);
	velocities.resize(numParticles, vec3(0.f));
	forceAccumulators.resize(numParticles, vec3(0.f));

	random_number_generator rng = { 1578123 };

	for (uint32 y = 0; y < gridSizeY; ++y)
	{
		float invMass = (y == 0) ? 0.f : invMassPerParticle; // Lock upper row.

		for (uint32 x = 0; x < gridSizeX; ++x)
		{
			float relX = x / (float)(gridSizeX - 1);
			float relY = y / (float)(gridSizeY - 1);
			
			vec3 position = getParticlePosition(relX, relY);
			positions.push_back(position);
			prevPositions.push_back(position);
			invMasses.push_back(invMass);
		}
	}

	for (uint32 y = 0; y < gridSizeY; ++y)
	{
		for (uint32 x = 0; x < gridSizeX; ++x)
		{
			uint32 index = y * gridSizeX + x;

			// Stretch constraints: direct right and bottom neighbor.
			if (x < gridSizeX - 1)
			{
				addConstraint(index, index + 1);
			}
			if (y < gridSizeY - 1)
			{
				addConstraint(index, index + gridSizeX);
			}

			// Shear constraints: direct diagonal neighbor.
			if (x < gridSizeX - 1 && y < gridSizeY - 1)
			{
				addConstraint(index, index + gridSizeX + 1);
				addConstraint(index + gridSizeX, index + 1);
			}

			// Bend constraints: neighbor right and bottom two places away.
			if (x < gridSizeX - 2)
			{
				addConstraint(index, index + 2);
			}
			if (y < gridSizeY - 2)
			{
				addConstraint(index, index + gridSizeX * 2);
			}
		}
	}
}

void cloth_component::setWorldPositionOfFixedVertices(const trs& transform)
{
	// Currently the top row is fixed, so transform this.
	for (uint32 x = 0; x < gridSizeX; ++x)
	{
		float relX = x / (float)(gridSizeX - 1);
		float relY = 0.f;
		vec3 localPosition = getParticlePosition(relX, relY);
		positions[x] = transformPosition(transform, localPosition);
	}
}

vec3 cloth_component::getParticlePosition(float relX, float relY)
{
	vec3 position = vec3(relX * width, -relY * height, 0.f);
	position.x -= width * 0.5f;
	std::swap(position.y, position.z);
	return position;
}

static vec3 calculateNormal(vec3 a, vec3 b, vec3 c)
{
	return cross(b - a, c - a);
}

void cloth_component::applyWindForce(vec3 force)
{
	for (uint32 y = 0; y < gridSizeY - 1; ++y)
	{
		for (uint32 x = 0; x < gridSizeX - 1; ++x)
		{
			uint32 tlIndex = y * gridSizeX + x;
			uint32 trIndex = tlIndex + 1;
			uint32 blIndex = tlIndex + gridSizeX;
			uint32 brIndex = blIndex + 1;

			vec3& tlForce = forceAccumulators[tlIndex];
			vec3& trForce = forceAccumulators[trIndex];
			vec3& blForce = forceAccumulators[blIndex];
			vec3& brForce = forceAccumulators[brIndex];

			{
				vec3 normal = calculateNormal(positions[tlIndex], positions[blIndex], positions[trIndex]);
				vec3 forceInNormalDir = normal * dot(normalize(normal), force);
				forceInNormalDir *= 1.f / 3.f;
				tlForce += forceInNormalDir;
				trForce += forceInNormalDir;
				blForce += forceInNormalDir;
			}

			{
				vec3 normal = calculateNormal(positions[brIndex], positions[trIndex], positions[blIndex]);
				vec3 forceInNormalDir = normal * dot(normalize(normal), force);
				forceInNormalDir *= 1.f / 3.f;
				brForce += forceInNormalDir;
				trForce += forceInNormalDir;
				blForce += forceInNormalDir;
			}
		}
	}
}

uint32 cloth_component::getRenderableVertexCount() const
{
	return gridSizeX * gridSizeY;
}

uint32 cloth_component::getRenderableTriangleCount() const
{
	return (gridSizeX - 1)* (gridSizeY - 1) * 2;
}

submesh_info cloth_component::getRenderData(vec3* positions, vertex_uv_normal_tangent* others, indexed_triangle16* triangles) const
{
	for (uint32 y = 0, i = 0; y < gridSizeY; ++y)
	{
		for (uint32 x = 0; x < gridSizeX; ++x, ++i)
		{
			float u = x / (float)(gridSizeX - 1);
			float v = y / (float)(gridSizeY - 1);

			positions[i] = this->positions[i];
			others[i].normal = vec3(0.f);
			others[i].tangent = vec3(0.f);
			others[i].uv = vec2(u, v);
		}
	}

	for (uint32 y = 0; y < gridSizeY - 1; ++y)
	{
		for (uint32 x = 0; x < gridSizeX - 1; ++x)
		{
			uint32 tlIndex = y * gridSizeX + x;
			uint32 trIndex = tlIndex + 1;
			uint32 blIndex = tlIndex + gridSizeX;
			uint32 brIndex = blIndex + 1;

			vec3 tl = positions[tlIndex];
			vec3 tr = positions[trIndex];
			vec3 bl = positions[blIndex];
			vec3 br = positions[brIndex];

			// Normal.
			{
				vec3 normal = calculateNormal(tl, bl, tr);
				others[tlIndex].normal += normal;
				others[trIndex].normal += normal;
				others[blIndex].normal += normal;
			}

			{
				vec3 normal = calculateNormal(br, tr, bl);
				others[brIndex].normal += normal;
				others[trIndex].normal += normal;
				others[blIndex].normal += normal;
			}


			// Tangent.
			{
				vec3 edge0 = tr - tl;
				vec3 edge1 = bl - tl;
				vec2 deltaUV0 = others[trIndex].uv - others[tlIndex].uv;
				vec2 deltaUV1 = others[blIndex].uv - others[tlIndex].uv;

				float f = 1.f / cross(deltaUV0, deltaUV1);
				vec3 tangent = f * (deltaUV1.y * edge0 - deltaUV0.y * edge1);
				others[tlIndex].tangent += tangent;
				others[trIndex].tangent += tangent;
				others[blIndex].tangent += tangent;
			}

			{
				vec3 edge0 = tr - br;
				vec3 edge1 = bl - br;
				vec2 deltaUV0 = others[trIndex].uv - others[brIndex].uv;
				vec2 deltaUV1 = others[blIndex].uv - others[brIndex].uv;

				float f = 1.f / cross(deltaUV0, deltaUV1);
				vec3 tangent = f * (deltaUV1.y * edge0 - deltaUV0.y * edge1);
				others[brIndex].tangent += tangent;
				others[trIndex].tangent += tangent;
				others[blIndex].tangent += tangent;
			}
		}
	}

	/* Will get normalized in shader anyway.
	for (uint32 i = 0; i < gridSizeX * gridSizeY; ++i)
	{
		others[i].normal = normalize(others[i].normal);
		others[i].tangent = normalize(others[i].tangent);
	}
	*/

	for (uint32 y = 0; y < gridSizeY - 1; ++y)
	{
		for (uint32 x = 0; x < gridSizeX - 1; ++x)
		{
			uint16 tlIndex = y * gridSizeX + x;
			uint16 trIndex = tlIndex + 1;
			uint16 blIndex = tlIndex + gridSizeX;
			uint16 brIndex = blIndex + 1;

			*triangles++ = { tlIndex, blIndex, brIndex };
			*triangles++ = { tlIndex, brIndex, trIndex };
		}
	}

	submesh_info result;
	result.baseVertex = 0;
	result.firstTriangle = 0;
	result.numTriangles = getRenderableTriangleCount();
	result.numVertices = gridSizeX * gridSizeY;
	return result;
}

struct cloth_constraint_temp
{
	vec3 gradient;
	float inverseScaledGradientSquared;
};

void cloth_component::simulate(uint32 velocityIterations, uint32 positionIterations, uint32 driftIterations, float dt)
{
	float gravityVelocity = GRAVITY * dt * gravityFactor;
	uint32 numParticles = gridSizeX * gridSizeY;

	for (uint32 i = 0; i < numParticles; ++i)
	{
		vec3& position = positions[i];
		vec3& prevPosition = prevPositions[i];
		float invMass = invMasses[i];
		vec3& velocity = velocities[i];
		vec3& force = forceAccumulators[i];

		if (invMass > 0.f)
		{
			velocity.y += gravityVelocity;
		}

		velocity += force * (invMass * dt);

		prevPosition = position;
		position += velocity * dt;
		force = vec3(0.f);
	}

	float invDt = (dt > 1e-5f) ? (1.f / dt) : 1.f;
	
	// Solve velocities.
	if (velocityIterations > 0)
	{
		std::vector<cloth_constraint_temp> constraintsTemp;
		constraintsTemp.reserve(constraints.size());

		for (cloth_constraint& c : constraints)
		{
			vec3 prevPositionA = prevPositions[c.a];
			vec3 prevPositionB = prevPositions[c.b];

			cloth_constraint_temp temp;
			temp.gradient = prevPositionB - prevPositionA;
			temp.inverseScaledGradientSquared = (c.inverseMassSum == 0.f) ? 0.f : (1.f / (squaredLength(temp.gradient) * c.inverseMassSum));
			constraintsTemp.push_back(temp);
		}


		for (uint32 it = 0; it < velocityIterations; ++it)
		{
			solveVelocities(constraintsTemp);
		}

		for (uint32 i = 0; i < numParticles; ++i)
		{
			positions[i] = prevPositions[i] + velocities[i] * dt;
		}
	}

	// Solve positions.
	if (positionIterations > 0)
	{
		for (uint32 it = 0; it < positionIterations; ++it)
		{
			solvePositions();
		}

		for (uint32 i = 0; i < numParticles; ++i)
		{
			velocities[i] = (positions[i] - prevPositions[i]) * invDt;
		}
	}

	// Solve drift.
	if (driftIterations > 0)
	{
		for (uint32 i = 0; i < numParticles; ++i)
		{
			prevPositions[i] = positions[i];
		}

		for (uint32 it = 0; it < driftIterations; ++it)
		{
			solvePositions();
		}

		for (uint32 i = 0; i < numParticles; ++i)
		{
			velocities[i] += (positions[i] - prevPositions[i]) * invDt;
		}
	}

	// Damping.
	float dampingFactor = 1.f / (1.f + dt * damping);
	for (uint32 i = 0; i < numParticles; ++i)
	{
		velocities[i] *= dampingFactor;
	}
}

void cloth_component::solveVelocities(const std::vector<cloth_constraint_temp>& constraintsTemp)
{
	for (uint32 i = 0; i < (uint32)constraints.size(); ++i)
	{
		cloth_constraint& c = constraints[i];
		const cloth_constraint_temp& temp = constraintsTemp[i];
		float j = -dot(temp.gradient, velocities[c.a] - velocities[c.b]) * temp.inverseScaledGradientSquared;
		velocities[c.a] += temp.gradient * (j * invMasses[c.a]);
		velocities[c.b] -= temp.gradient * (j * invMasses[c.b]);
	}
}

void cloth_component::solvePositions()
{
	for (cloth_constraint& c : constraints)
	{
		if (c.inverseMassSum > 0.f)
		{
			vec3 delta = positions[c.b] - positions[c.a];
			float len = squaredLength(delta);

			float sqRestDistance = c.restDistance * c.restDistance;
			if (sqRestDistance + len > 1e-5f)
			{
				float k = ((sqRestDistance - len) / (c.inverseMassSum * (sqRestDistance + len)));
				positions[c.a] -= delta * (k * invMasses[c.a]);
				positions[c.b] += delta * (k * invMasses[c.b]);
			}
		}
	}
}

void cloth_component::addConstraint(uint32 indexA, uint32 indexB)
{
	constraints.push_back(cloth_constraint
		{ 
			indexA, 
			indexB, 
			length(positions[indexA] - positions[indexB]),
			(invMasses[indexA] + invMasses[indexB]) / stiffness,
		});
}

