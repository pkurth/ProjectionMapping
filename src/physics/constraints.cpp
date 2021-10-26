#include "pch.h"
#include "constraints.h"
#include "physics.h"
#include "collision_narrow.h"
#include "core/cpu_profiling.h"
#include "core/math_simd.h"


#define DISTANCE_CONSTRAINT_BETA 0.1f
#define BALL_CONSTRAINT_BETA 0.1f
#define SLIDER_CONSTRAINT_BETA 0.1f
#define HINGE_ROTATION_CONSTRAINT_BETA 0.3f
#define HINGE_LIMIT_CONSTRAINT_BETA 0.1f
#define TWIST_LIMIT_CONSTRAINT_BETA 0.1f
#define SLIDER_LIMIT_CONSTRAINT_BETA 0.1f

#define DT_THRESHOLD 1e-5f


#if CONSTRAINT_SIMD_WIDTH == 4
typedef vec2x<floatx4> vec2w;
typedef vec3x<floatx4> vec3w;
typedef vec4x<floatx4> vec4w;
typedef quatx<floatx4> quatw;
typedef mat2x<floatx4> mat2w;
typedef mat3x<floatx4> mat3w;
typedef floatx4 floatw;
typedef intx4 intw;
#elif CONSTRAINT_SIMD_WIDTH == 8 && defined(SIMD_AVX_2)
typedef vec2x<floatx8> vec2w;
typedef vec3x<floatx8> vec3w;
typedef vec4x<floatx8> vec4w;
typedef quatx<floatx8> quatw;
typedef mat2x<floatx8> mat2w;
typedef mat3x<floatx8> mat3w;
typedef floatx8 floatw;
typedef intx8 intw;
#elif CONSTRAINT_SIMD_WIDTH == 16 && defined(SIMD_AVX_512)
typedef vec2x<floatx16> vec2w;
typedef vec3x<floatx16> vec3w;
typedef vec4x<floatx16> vec4w;
typedef quatx<floatx16> quatw;
typedef mat2x<floatx16> mat2w;
typedef mat3x<floatx16> mat3w;
typedef floatx16 floatw;
typedef intx16 intw;
#else
#error Invalid SIMD width.
#endif




struct alignas(32) simd_constraint_body_pair
{
	uint32 ab[CONSTRAINT_SIMD_WIDTH];
};

struct alignas(32) simd_constraint_slot
{
	uint32 indices[CONSTRAINT_SIMD_WIDTH];
};

static uint32 scheduleConstraintsSIMD(memory_arena& arena, const constraint_body_pair* bodyPairs, uint32 numBodyPairs, uint16 dummyRigidBodyIndex, simd_constraint_slot* outConstraintSlots)
{
	CPU_PROFILE_BLOCK("Schedule constraints SIMD");

	uint32 numConstraintSlots = 0;

	static const uint32 numBuckets = 4;
	simd_constraint_body_pair* pairBuckets[numBuckets];
	simd_constraint_slot* slotBuckets[numBuckets];
	uint32 numEntriesPerBucket[numBuckets] = {};

	intw invalid = ~0;

	uint32 numAllocationsPerBucket = bucketize(numBodyPairs, numBuckets);

	memory_marker marker = arena.getMarker();

	for (unsigned i = 0; i < numBuckets; ++i)
	{
		pairBuckets[i] = arena.allocate<simd_constraint_body_pair>(numAllocationsPerBucket + 1);
		slotBuckets[i] = arena.allocate<simd_constraint_slot>(numAllocationsPerBucket);

		// Add padding with invalid data so we don't have to range check.
		invalid.store((int*)pairBuckets[i]->ab);
	}

	for (uint32 i = 0; i < numBodyPairs; ++i)
	{
		constraint_body_pair bodyPair = bodyPairs[i];

		// If one of the bodies is the dummy, just set it to the other for the comparison below.
		uint16 rbA = (bodyPair.rbA == dummyRigidBodyIndex) ? bodyPair.rbB : bodyPair.rbA;
		uint16 rbB = (bodyPair.rbB == dummyRigidBodyIndex) ? bodyPair.rbA : bodyPair.rbB;

		uint32 bucket = i % numBuckets;
		simd_constraint_body_pair* pairs = pairBuckets[bucket];
		simd_constraint_slot* slots = slotBuckets[bucket];


#if CONSTRAINT_SIMD_WIDTH == 4
		intw a = _mm_set1_epi16(rbA);
		intw b = _mm_set1_epi16(rbB);
		intw scheduled;

		uint32 j = 0;
		for (;; ++j)
		{
			scheduled = _mm_load_si128((const __m128i*)pairs[j].ab);

			__m128i conflictsWithThisSlot = _mm_packs_epi16(_mm_cmpeq_epi16(a, scheduled), _mm_cmpeq_epi16(b, scheduled));
			if (!_mm_movemask_epi8(conflictsWithThisSlot))
			{
				break;
			}
		}
#else
		intw a = _mm256_set1_epi16(rbA);
		intw b = _mm256_set1_epi16(rbB);
		intw scheduled;

		uint32 j = 0;
		for (;; ++j)
		{
			scheduled = _mm256_load_si256((const __m256i*)pairs[j].ab);

			__m256i conflictsWithThisSlot = _mm256_packs_epi16(_mm256_cmpeq_epi16(a, scheduled), _mm256_cmpeq_epi16(b, scheduled));
			if (!_mm256_movemask_epi8(conflictsWithThisSlot))
			{
				break;
			}
		}

#endif


		uint32 lane = indexOfLeastSignificantSetBit(toBitMask(reinterpret(scheduled == invalid)));

		simd_constraint_body_pair* pair = pairs + j;
		simd_constraint_slot* slot = slots + j;

		slot->indices[lane] = i;
		pair->ab[lane] = ((uint32)bodyPair.rbA << 16) | bodyPair.rbB; // Use the original indices here.

		uint32& count = numEntriesPerBucket[bucket];
		if (j == count)
		{
			// We used a new entry.
			++count;

			// Set entry at end to invalid.
			invalid.store((int*)pairs[count].ab);
		}
		else if (lane == CONSTRAINT_SIMD_WIDTH - 1)
		{
			// This entry is full -> commit it.
			intw indices = (int32*)slot->indices;

			// Swap and pop.
			--count;
			*pair = pairs[count];
			*slot = slots[count];

			indices.store((int32*)outConstraintSlots[numConstraintSlots++].indices);

			// Set entry at end to invalid.
			invalid.store((int*)pairs[count].ab);
		}
	}

	// There are still entries left, where not all lanes are filled. We replace these indices with the first in this entry.

	for (uint32 bucket = 0; bucket < numBuckets; ++bucket)
	{
		simd_constraint_body_pair* pairs = pairBuckets[bucket];
		simd_constraint_slot* slots = slotBuckets[bucket];
		uint32 count = numEntriesPerBucket[bucket];

		for (uint32 i = 0; i < count; ++i)
		{
			intw ab = (int32*)pairs[i].ab;
			intw indices = (int32_t*)slots[i].indices;

			intw firstIndex = fillWithFirstLane(indices);

			auto mask = ab == invalid;
			indices = ifThen(mask, firstIndex, indices);
			indices.store((int32*)outConstraintSlots[numConstraintSlots++].indices);
		}
	}

	arena.resetToMarker(marker);

	return numConstraintSlots;
}




distance_constraint_solver initializeDistanceVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const distance_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize distance constraints");

	float invDt = 1.f / dt;

	distance_constraint_update* constraints = arena.allocate<distance_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const distance_constraint& in = input[i];
		distance_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		out.relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		out.relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + out.relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + out.relGlobalAnchorB;

		out.u = globalAnchorB - globalAnchorA;
		float l = length(out.u);
		out.u = (l > 0.001f) ? (out.u * (1.f / l)) : vec3(0.f);

		vec3 crAu = cross(out.relGlobalAnchorA, out.u);
		vec3 crBu = cross(out.relGlobalAnchorB, out.u);
		float invMass = globalA.invMass + dot(crAu, globalA.invInertia * crAu)
					  + globalB.invMass + dot(crBu, globalB.invInertia * crBu);
		out.effectiveMass = (invMass != 0.f) ? (1.f / invMass) : 0.f;

		out.bias = 0.f;
		if (dt > DT_THRESHOLD)
		{
			out.bias = (l - in.globalLength) * (DISTANCE_CONSTRAINT_BETA * invDt);
		}

		out.impulseToAngularVelocityA = globalA.invInertia * cross(out.relGlobalAnchorA, crAu);
		out.impulseToAngularVelocityB = globalB.invInertia * cross(out.relGlobalAnchorB, crBu);
	}

	distance_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveDistanceVelocityConstraints(distance_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve distance constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		distance_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		vec3 anchorVelocityA = rbA.linearVelocity + cross(rbA.angularVelocity, con.relGlobalAnchorA);
		vec3 anchorVelocityB = rbB.linearVelocity + cross(rbB.angularVelocity, con.relGlobalAnchorB);
		float Cdot = dot(con.u, anchorVelocityB - anchorVelocityA) + con.bias;

		float lambda = -con.effectiveMass * Cdot;
		vec3 P = lambda * con.u;
		rbA.linearVelocity -= rbA.invMass * P;
		rbA.angularVelocity -= con.impulseToAngularVelocityA * lambda;
		rbB.linearVelocity += rbB.invMass * P;
		rbB.angularVelocity += con.impulseToAngularVelocityB * lambda;
	}
}

simd_distance_constraint_solver initializeDistanceVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const distance_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize distance constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_distance_constraint_batch* batches = arena.allocate<simd_distance_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_distance_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}


		vec3w localAnchorA, localAnchorB;
		floatw globalLength;
		floatw dummy;
		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			localAnchorA.x, localAnchorA.y, localAnchorA.z, localAnchorB.x, localAnchorB.y, localAnchorB.z, globalLength, dummy);


		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);


		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;

		vec3w u = globalAnchorB - globalAnchorA;
		floatw l = length(u);
		u = ifThen(l > 0.001f, u * (1.f / l), vec3w::zero());

		vec3w crAu = cross(relGlobalAnchorA, u);
		vec3w crBu = cross(relGlobalAnchorB, u);
		floatw invMass = invMassA + dot(crAu, invInertiaA * crAu)
					   + invMassB + dot(crBu, invInertiaB * crBu);
		floatw effectiveMass = ifThen(invMass != zero, 1.f / invMass, zero);

		floatw bias = zero;
		if (dt > DT_THRESHOLD)
		{
			bias = (l - globalLength) * (floatw(DISTANCE_CONSTRAINT_BETA) * invDt);
		}

		vec3w impulseToAngularVelocityA = invInertiaA * cross(relGlobalAnchorA, crAu);
		vec3w impulseToAngularVelocityB = invInertiaB * cross(relGlobalAnchorB, crBu);


		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		impulseToAngularVelocityA.x.store(batch.impulseToAngularVelocityA[0]);
		impulseToAngularVelocityA.y.store(batch.impulseToAngularVelocityA[1]);
		impulseToAngularVelocityA.z.store(batch.impulseToAngularVelocityA[2]);

		impulseToAngularVelocityB.x.store(batch.impulseToAngularVelocityB[0]);
		impulseToAngularVelocityB.y.store(batch.impulseToAngularVelocityB[1]);
		impulseToAngularVelocityB.z.store(batch.impulseToAngularVelocityB[2]);

		u.x.store(batch.u[0]);
		u.y.store(batch.u[1]);
		u.z.store(batch.u[2]);

		bias.store(batch.bias);
		effectiveMass.store(batch.effectiveMass);
	}

	simd_distance_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveDistanceVelocityConstraintsSIMD(simd_distance_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve distance constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_distance_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		floatw dummyA;

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			dummyA, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		floatw dummyB;

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			dummyB, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Load constraint.
		vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
		vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
		vec3w impulseToAngularVelocityA(batch.impulseToAngularVelocityA[0], batch.impulseToAngularVelocityA[1], batch.impulseToAngularVelocityA[2]);
		vec3w impulseToAngularVelocityB(batch.impulseToAngularVelocityB[0], batch.impulseToAngularVelocityB[1], batch.impulseToAngularVelocityB[2]);
		vec3w u(batch.u[0], batch.u[1], batch.u[2]);
		floatw bias(batch.bias);
		floatw effectiveMass(batch.effectiveMass);

		vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
		vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);
		floatw Cdot = dot(u, anchorVelocityB - anchorVelocityA) + bias;

		floatw lambda = -effectiveMass * Cdot;
		vec3w P = lambda * u;
		vA -= invMassA * P;
		wA -= impulseToAngularVelocityA * lambda;
		vB += invMassB * P;
		wB += impulseToAngularVelocityB * lambda;


		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			dummyA, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			dummyB, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}




ball_constraint_solver initializeBallVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const ball_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize ball constraints");

	float invDt = 1.f / dt;

	ball_constraint_update* constraints = arena.allocate<ball_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const ball_constraint& in = input[i];
		ball_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		out.relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		out.relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + out.relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + out.relGlobalAnchorB;

		mat3 skewMatA = getSkewMatrix(out.relGlobalAnchorA);
		mat3 skewMatB = getSkewMatrix(out.relGlobalAnchorB);
		
		out.invEffectiveMass = skewMatA * globalA.invInertia * transpose(skewMatA)
							 + skewMatB * globalB.invInertia * transpose(skewMatB)
							 + mat3::identity * (globalA.invMass + globalB.invMass);

		out.bias = 0.f;
		if (dt > DT_THRESHOLD)
		{
			out.bias = (globalAnchorB - globalAnchorA) * (BALL_CONSTRAINT_BETA * invDt);
		}
	}

	ball_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveBallVelocityConstraints(ball_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve ball constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		ball_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		vec3 anchorVelocityA = rbA.linearVelocity + cross(rbA.angularVelocity, con.relGlobalAnchorA);
		vec3 anchorVelocityB = rbB.linearVelocity + cross(rbB.angularVelocity, con.relGlobalAnchorB);
		vec3 Cdot = anchorVelocityB - anchorVelocityA + con.bias;

		vec3 P = solveLinearSystem(con.invEffectiveMass, -Cdot);
		rbA.linearVelocity -= rbA.invMass * P;
		rbA.angularVelocity -= rbA.invInertia * cross(con.relGlobalAnchorA, P);
		rbB.linearVelocity += rbB.invMass * P;
		rbB.angularVelocity += rbB.invInertia * cross(con.relGlobalAnchorB, P);
	}
}

simd_ball_constraint_solver initializeBallVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const ball_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize distance constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_ball_constraint_batch* batches = arena.allocate<simd_ball_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;
	const floatw one = 1.f;

	const mat3w identity(one, zero, zero,
		zero, one, zero,
		zero, zero, one);

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_ball_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}

		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);



		vec3w localAnchorA, localAnchorB;
		floatw dummy0, dummy1;
		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			localAnchorA.x, localAnchorA.y, localAnchorA.z, localAnchorB.x, localAnchorB.y, localAnchorB.z, dummy0, dummy1);


		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;

		mat3w skewMatA = getSkewMatrix(relGlobalAnchorA);
		mat3w skewMatB = getSkewMatrix(relGlobalAnchorB);
		
		mat3w invEffectiveMass = skewMatA * invInertiaA * transpose(skewMatA)
							   + skewMatB * invInertiaB * transpose(skewMatB);
		floatw invMassSum = invMassA + invMassB;
		invEffectiveMass.m00 += invMassSum;
		invEffectiveMass.m11 += invMassSum;
		invEffectiveMass.m22 += invMassSum;

		vec3w bias = zero;
		if (dt > DT_THRESHOLD)
		{
			bias = (globalAnchorB - globalAnchorA) * (floatw(BALL_CONSTRAINT_BETA) * invDt);
		}

		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		bias.x.store(batch.bias[0]);
		bias.y.store(batch.bias[1]);
		bias.z.store(batch.bias[2]);

		invEffectiveMass.m[0].store(batch.invEffectiveMass[0]);
		invEffectiveMass.m[1].store(batch.invEffectiveMass[1]);
		invEffectiveMass.m[2].store(batch.invEffectiveMass[2]);
		invEffectiveMass.m[3].store(batch.invEffectiveMass[3]);
		invEffectiveMass.m[4].store(batch.invEffectiveMass[4]);
		invEffectiveMass.m[5].store(batch.invEffectiveMass[5]);
		invEffectiveMass.m[6].store(batch.invEffectiveMass[6]);
		invEffectiveMass.m[7].store(batch.invEffectiveMass[7]);
		invEffectiveMass.m[8].store(batch.invEffectiveMass[8]);
	}

	simd_ball_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveBallVelocityConstraintsSIMD(simd_ball_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve ball constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_ball_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		mat3w invInertiaA;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21,
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		mat3w invInertiaB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Load constraint.
		vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
		vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
		vec3w bias(batch.bias[0], batch.bias[1], batch.bias[2]);
		mat3w invEffectiveMass;
		invEffectiveMass.m[0] = floatw(batch.invEffectiveMass[0]);
		invEffectiveMass.m[1] = floatw(batch.invEffectiveMass[1]);
		invEffectiveMass.m[2] = floatw(batch.invEffectiveMass[2]);
		invEffectiveMass.m[3] = floatw(batch.invEffectiveMass[3]);
		invEffectiveMass.m[4] = floatw(batch.invEffectiveMass[4]);
		invEffectiveMass.m[5] = floatw(batch.invEffectiveMass[5]);
		invEffectiveMass.m[6] = floatw(batch.invEffectiveMass[6]);
		invEffectiveMass.m[7] = floatw(batch.invEffectiveMass[7]);
		invEffectiveMass.m[8] = floatw(batch.invEffectiveMass[8]);

		vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
		vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);
		vec3w Cdot = anchorVelocityB - anchorVelocityA + bias;

		vec3w P = solveLinearSystem(invEffectiveMass, -Cdot);
		vA -= invMassA * P;
		wA -= invInertiaA * cross(relGlobalAnchorA, P);
		vB += invMassB * P;
		wB += invInertiaB * cross(relGlobalAnchorB, P);


		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}


fixed_constraint_solver initializeFixedVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const fixed_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize fixed constraints");

	float invDt = 1.f / dt;

	fixed_constraint_update* constraints = arena.allocate<fixed_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const fixed_constraint& in = input[i];
		fixed_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		out.relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		out.relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + out.relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + out.relGlobalAnchorB;

		mat3 skewMatA = getSkewMatrix(out.relGlobalAnchorA);
		mat3 skewMatB = getSkewMatrix(out.relGlobalAnchorB);

		out.invEffectiveTranslationMass = skewMatA * globalA.invInertia * transpose(skewMatA)
										+ skewMatB * globalB.invInertia * transpose(skewMatB)
										+ mat3::identity * (globalA.invMass + globalB.invMass);

		out.invEffectiveRotationMass = globalA.invInertia + globalB.invInertia;

		out.translationBias = vec3(0.f, 0.f, 0.f);
		out.rotationBias = vec3(0.f, 0.f, 0.f);

		if (dt > DT_THRESHOLD)
		{
			out.translationBias = (globalAnchorB - globalAnchorA) * (BALL_CONSTRAINT_BETA * invDt);

			quat rotationError = globalB.rotation * in.initialInvRotationDifference * conjugate(globalA.rotation);
			out.rotationBias = rotationError.v * (SLIDER_CONSTRAINT_BETA * invDt * 2.f);
		}
	}

	fixed_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveFixedVelocityConstraints(fixed_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve fixed constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		fixed_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		// Rotation part.
		{
			vec3 Cdot = rbB.angularVelocity - rbA.angularVelocity;

			vec3 rotationLambda = solveLinearSystem(con.invEffectiveRotationMass, -(Cdot + con.rotationBias));
			rbA.angularVelocity -= rbA.invInertia * rotationLambda;
			rbB.angularVelocity += rbB.invInertia * rotationLambda;
		}

		// Position part.
		{
			vec3 anchorVelocityA = rbA.linearVelocity + cross(rbA.angularVelocity, con.relGlobalAnchorA);
			vec3 anchorVelocityB = rbB.linearVelocity + cross(rbB.angularVelocity, con.relGlobalAnchorB);
			vec3 Cdot = anchorVelocityB - anchorVelocityA + con.translationBias;

			vec3 P = solveLinearSystem(con.invEffectiveTranslationMass, -Cdot);
			rbA.linearVelocity -= rbA.invMass * P;
			rbA.angularVelocity -= rbA.invInertia * cross(con.relGlobalAnchorA, P);
			rbB.linearVelocity += rbB.invMass * P;
			rbB.angularVelocity += rbB.invInertia * cross(con.relGlobalAnchorB, P);
		}
	}
}

simd_fixed_constraint_solver initializeFixedVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const fixed_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize fixed constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_fixed_constraint_batch* batches = arena.allocate<simd_fixed_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;
	const floatw one = 1.f;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_fixed_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}


		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);


		quatw initialInvRotationDifference;
		vec3w localAnchorA, localAnchorB;
		floatw dummy0, dummy1;
		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			initialInvRotationDifference.x, initialInvRotationDifference.y, initialInvRotationDifference.z, initialInvRotationDifference.w,
			localAnchorA.x, localAnchorA.y, localAnchorA.z, localAnchorB.x);

		load4(&input->localAnchorB.y, constraintIndices, (uint32)sizeof(*input),
			localAnchorB.y, localAnchorB.z, dummy0, dummy1);


		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;

		mat3w skewMatA = getSkewMatrix(relGlobalAnchorA);
		mat3w skewMatB = getSkewMatrix(relGlobalAnchorB);

		mat3w invEffectiveTranslationMass = skewMatA * invInertiaA * transpose(skewMatA)
										  + skewMatB * invInertiaB * transpose(skewMatB);
		floatw invMassSum = invMassA + invMassB;
		invEffectiveTranslationMass.m00 += invMassSum;
		invEffectiveTranslationMass.m11 += invMassSum;
		invEffectiveTranslationMass.m22 += invMassSum;

		mat3w invEffectiveRotationMass = invInertiaA + invInertiaB;

		vec3w translationBias = zero;
		vec3w rotationBias = zero;

		if (dt > DT_THRESHOLD)
		{
			translationBias = (globalAnchorB - globalAnchorA) * (floatw(BALL_CONSTRAINT_BETA) * invDt);

			quatw rotationError = rotationB * initialInvRotationDifference * conjugate(rotationA);
			rotationBias = rotationError.v * (SLIDER_CONSTRAINT_BETA * 2.f * invDt);
		}

		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		invEffectiveTranslationMass.m[0].store(batch.invEffectiveTranslationMass[0]);
		invEffectiveTranslationMass.m[1].store(batch.invEffectiveTranslationMass[1]);
		invEffectiveTranslationMass.m[2].store(batch.invEffectiveTranslationMass[2]);
		invEffectiveTranslationMass.m[3].store(batch.invEffectiveTranslationMass[3]);
		invEffectiveTranslationMass.m[4].store(batch.invEffectiveTranslationMass[4]);
		invEffectiveTranslationMass.m[5].store(batch.invEffectiveTranslationMass[5]);
		invEffectiveTranslationMass.m[6].store(batch.invEffectiveTranslationMass[6]);
		invEffectiveTranslationMass.m[7].store(batch.invEffectiveTranslationMass[7]);
		invEffectiveTranslationMass.m[8].store(batch.invEffectiveTranslationMass[8]);

		translationBias.x.store(batch.translationBias[0]);
		translationBias.y.store(batch.translationBias[1]);
		translationBias.z.store(batch.translationBias[2]);

		invEffectiveRotationMass.m[0].store(batch.invEffectiveRotationMass[0]);
		invEffectiveRotationMass.m[1].store(batch.invEffectiveRotationMass[1]);
		invEffectiveRotationMass.m[2].store(batch.invEffectiveRotationMass[2]);
		invEffectiveRotationMass.m[3].store(batch.invEffectiveRotationMass[3]);
		invEffectiveRotationMass.m[4].store(batch.invEffectiveRotationMass[4]);
		invEffectiveRotationMass.m[5].store(batch.invEffectiveRotationMass[5]);
		invEffectiveRotationMass.m[6].store(batch.invEffectiveRotationMass[6]);
		invEffectiveRotationMass.m[7].store(batch.invEffectiveRotationMass[7]);
		invEffectiveRotationMass.m[8].store(batch.invEffectiveRotationMass[8]);

		rotationBias.x.store(batch.rotationBias[0]);
		rotationBias.y.store(batch.rotationBias[1]);
		rotationBias.z.store(batch.rotationBias[2]);
	}

	simd_fixed_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveFixedVelocityConstraintsSIMD(simd_fixed_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve fixed constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_fixed_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		mat3w invInertiaA;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21,
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		mat3w invInertiaB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Rotation part.
		{
			vec3w rotationBias(batch.rotationBias[0], batch.rotationBias[1], batch.rotationBias[2]);
			mat3w invEffectiveRotationMass;
			invEffectiveRotationMass.m[0] = floatw(batch.invEffectiveRotationMass[0]);
			invEffectiveRotationMass.m[1] = floatw(batch.invEffectiveRotationMass[1]);
			invEffectiveRotationMass.m[2] = floatw(batch.invEffectiveRotationMass[2]);
			invEffectiveRotationMass.m[3] = floatw(batch.invEffectiveRotationMass[3]);
			invEffectiveRotationMass.m[4] = floatw(batch.invEffectiveRotationMass[4]);
			invEffectiveRotationMass.m[5] = floatw(batch.invEffectiveRotationMass[5]);
			invEffectiveRotationMass.m[6] = floatw(batch.invEffectiveRotationMass[6]);
			invEffectiveRotationMass.m[7] = floatw(batch.invEffectiveRotationMass[7]);
			invEffectiveRotationMass.m[8] = floatw(batch.invEffectiveRotationMass[8]);

			vec3w Cdot = wB - wA;

			vec3w rotationLambda = solveLinearSystem(invEffectiveRotationMass, -(Cdot + rotationBias));
			wA -= invInertiaA * rotationLambda;
			wB += invInertiaB * rotationLambda;
		}


		// Position part.
		{
			vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
			vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
			vec3w translationBias(batch.translationBias[0], batch.translationBias[1], batch.translationBias[2]);
			mat3w invEffectiveTranslationMass;
			invEffectiveTranslationMass.m[0] = floatw(batch.invEffectiveTranslationMass[0]);
			invEffectiveTranslationMass.m[1] = floatw(batch.invEffectiveTranslationMass[1]);
			invEffectiveTranslationMass.m[2] = floatw(batch.invEffectiveTranslationMass[2]);
			invEffectiveTranslationMass.m[3] = floatw(batch.invEffectiveTranslationMass[3]);
			invEffectiveTranslationMass.m[4] = floatw(batch.invEffectiveTranslationMass[4]);
			invEffectiveTranslationMass.m[5] = floatw(batch.invEffectiveTranslationMass[5]);
			invEffectiveTranslationMass.m[6] = floatw(batch.invEffectiveTranslationMass[6]);
			invEffectiveTranslationMass.m[7] = floatw(batch.invEffectiveTranslationMass[7]);
			invEffectiveTranslationMass.m[8] = floatw(batch.invEffectiveTranslationMass[8]);


			vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
			vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);
			vec3w Cdot = anchorVelocityB - anchorVelocityA + translationBias;

			vec3w P = solveLinearSystem(invEffectiveTranslationMass, -Cdot);
			vA -= invMassA * P;
			wA -= invInertiaA * cross(relGlobalAnchorA, P);
			vB += invMassB * P;
			wB += invInertiaB * cross(relGlobalAnchorB, P);
		}


		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}



hinge_constraint_solver initializeHingeVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const hinge_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize hinge constraints");

	float invDt = 1.f / dt;

	hinge_constraint_update* constraints = arena.allocate<hinge_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const hinge_constraint& in = input[i];
		hinge_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		out.relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		out.relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + out.relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + out.relGlobalAnchorB;




		// Position part. Identical to ball.

		mat3 skewMatA = getSkewMatrix(out.relGlobalAnchorA);
		mat3 skewMatB = getSkewMatrix(out.relGlobalAnchorB);

		out.invEffectiveTranslationMass = skewMatA * globalA.invInertia * transpose(skewMatA)
										+ skewMatB * globalB.invInertia * transpose(skewMatB)
										+ mat3::identity * (globalA.invMass + globalB.invMass);

		out.translationBias = 0.f;
		if (dt > DT_THRESHOLD)
		{
			out.translationBias = (globalAnchorB - globalAnchorA) * (BALL_CONSTRAINT_BETA * invDt);
		}



		// Rotation part.
		vec3 globalHingeAxisA = globalA.rotation * in.localHingeAxisA;
		vec3 globalHingeAxisB = globalB.rotation * in.localHingeAxisB;

		vec3 globalTangentB, globalBitangentB;
		getTangents(globalHingeAxisB, globalTangentB, globalBitangentB);

		vec3 bxa = cross(globalTangentB, globalHingeAxisA);
		vec3 cxa = cross(globalBitangentB, globalHingeAxisA);
		vec3 iAbxa = globalA.invInertia * bxa;
		vec3 iBbxa = globalB.invInertia * bxa;
		vec3 iAcxa = globalA.invInertia * cxa;
		vec3 iBcxa = globalB.invInertia * cxa;

		out.invEffectiveRotationMass.m00 = dot(bxa, iAbxa) + dot(bxa, iBbxa);
		out.invEffectiveRotationMass.m01 = dot(bxa, iAcxa) + dot(bxa, iBcxa);
		out.invEffectiveRotationMass.m10 = dot(cxa, iAbxa) + dot(cxa, iBbxa);
		out.invEffectiveRotationMass.m11 = dot(cxa, iAcxa) + dot(cxa, iBcxa);

		out.bxa = bxa;
		out.cxa = cxa;

		out.rotationBias = vec2(0.f, 0.f);
		if (dt > DT_THRESHOLD)
		{
			out.rotationBias = vec2(dot(globalHingeAxisA, globalTangentB), dot(globalHingeAxisA, globalBitangentB)) * (HINGE_ROTATION_CONSTRAINT_BETA * invDt);
		}


		// Limits and motor.
		out.solveLimit = false;
		out.solveMotor = false;

		if (in.minRotationLimit <= 0.f || in.maxRotationLimit >= 0.f || in.maxMotorTorque > 0.f)
		{
			vec3 localHingeCompareA = conjugate(globalA.rotation) * (globalB.rotation * in.localHingeTangentB);
			float angle = atan2(dot(localHingeCompareA, in.localHingeBitangentA), dot(localHingeCompareA, in.localHingeTangentA));

			bool minLimitViolated = in.minRotationLimit <= 0.f && angle <= in.minRotationLimit;
			bool maxLimitViolated = in.maxRotationLimit >= 0.f && angle >= in.maxRotationLimit;

			assert(!(minLimitViolated && maxLimitViolated));

			out.solveLimit = minLimitViolated || maxLimitViolated;
			out.solveMotor = in.maxMotorTorque > 0.f;
			if (out.solveLimit || out.solveMotor)
			{
				out.globalRotationAxis = globalHingeAxisA;
				out.limitImpulse = 0.f;

				float invEffectiveAxialMass = dot(globalHingeAxisA, globalA.invInertia * globalHingeAxisA)
											+ dot(globalHingeAxisA, globalB.invInertia * globalHingeAxisA);

				out.effectiveAxialMass = (invEffectiveAxialMass != 0.f) ? (1.f / invEffectiveAxialMass) : 0.f;
				out.limitSign = minLimitViolated ? 1.f : -1.f;

				out.maxMotorImpulse = in.maxMotorTorque * dt;
				out.motorImpulse = 0.f;

				out.motorAndLimitImpulseToAngularVelocityA = globalA.invInertia * out.globalRotationAxis;
				out.motorAndLimitImpulseToAngularVelocityB = globalB.invInertia * out.globalRotationAxis;

				out.motorVelocity = in.motorVelocity;
				if (in.motorType == constraint_position_motor)
				{
					// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
					// This will later get clamped to the maximum motor impulse.
					float minLimit = (in.minRotationLimit <= 0.f) ? in.minRotationLimit : -M_PI;
					float maxLimit = (in.maxRotationLimit >= 0.f) ? in.maxRotationLimit : M_PI;
					float targetAngle = clamp(in.motorTargetAngle, minLimit, maxLimit);
					out.motorVelocity = (dt > DT_THRESHOLD) ? ((targetAngle - angle) * invDt) : 0.f;
				}

				out.limitBias = 0.f;
				if (dt > DT_THRESHOLD)
				{
					float d = minLimitViolated ? (angle - in.minRotationLimit) : (in.maxRotationLimit - angle);
					out.limitBias = d * HINGE_LIMIT_CONSTRAINT_BETA * invDt;
				}
			}
		}
	}

	hinge_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveHingeVelocityConstraints(hinge_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve hinge constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		hinge_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		vec3 vA = rbA.linearVelocity;
		vec3 wA = rbA.angularVelocity;
		vec3 vB = rbB.linearVelocity;
		vec3 wB = rbB.angularVelocity;

		// Solve in order of importance (most important last): Motor -> Limits -> Rotation -> Position.

		vec3 globalRotationAxis = con.globalRotationAxis;

		// Motor.
		if (con.solveMotor)
		{
			float aDotWA = dot(globalRotationAxis, wA); // How fast are we turning about the axis.
			float aDotWB = dot(globalRotationAxis, wB);

			float relAngularVelocity = (aDotWB - aDotWA);
			float motorCdot = relAngularVelocity - con.motorVelocity;

			float motorLambda = -con.effectiveAxialMass * motorCdot;
			float oldImpulse = con.motorImpulse;
			con.motorImpulse = clamp(con.motorImpulse + motorLambda, -con.maxMotorImpulse, con.maxMotorImpulse);
			motorLambda = con.motorImpulse - oldImpulse;

			wA -= con.motorAndLimitImpulseToAngularVelocityA * motorLambda;
			wB += con.motorAndLimitImpulseToAngularVelocityB * motorLambda;
		}

		// Limits.
		if (con.solveLimit)
		{
			float limitSign = con.limitSign;

			float aDotWA = dot(globalRotationAxis, wA); // How fast are we turning about the axis.
			float aDotWB = dot(globalRotationAxis, wB);
			float relAngularVelocity = limitSign * (aDotWB - aDotWA);

			float limitCdot = relAngularVelocity + con.limitBias;
			float limitLambda = -con.effectiveAxialMass * limitCdot;

			float impulse = max(con.limitImpulse + limitLambda, 0.f);
			limitLambda = impulse - con.limitImpulse;
			con.limitImpulse = impulse;

			limitLambda *= limitSign;

			wA -= con.motorAndLimitImpulseToAngularVelocityA * limitLambda;
			wB += con.motorAndLimitImpulseToAngularVelocityB * limitLambda;
		}

		// Rotation part.
		{
			vec3 deltaAngularVelocity = wB - wA;

			vec2 rotationCdot(dot(con.bxa, deltaAngularVelocity), dot(con.cxa, deltaAngularVelocity));
			vec2 rotLambda = solveLinearSystem(con.invEffectiveRotationMass, -(rotationCdot + con.rotationBias));

			vec3 rotationP = con.bxa * rotLambda.x + con.cxa * rotLambda.y;

			wA -= rbA.invInertia * rotationP;
			wB += rbB.invInertia * rotationP;
		}

		// Position part.
		{
			vec3 anchorVelocityA = vA + cross(wA, con.relGlobalAnchorA);
			vec3 anchorVelocityB = vB + cross(wB, con.relGlobalAnchorB);
			vec3 translationCdot = anchorVelocityB - anchorVelocityA + con.translationBias;

			vec3 translationP = solveLinearSystem(con.invEffectiveTranslationMass, -translationCdot);

			vA -= rbA.invMass * translationP;
			wA -= rbA.invInertia * cross(con.relGlobalAnchorA, translationP);
			vB += rbB.invMass * translationP;
			wB += rbB.invInertia * cross(con.relGlobalAnchorB, translationP);
		}

		rbA.linearVelocity = vA;
		rbA.angularVelocity = wA;
		rbB.linearVelocity = vB;
		rbB.angularVelocity = wB;
	}
}

simd_hinge_constraint_solver initializeHingeVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const hinge_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize hinge constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_hinge_constraint_batch* batches = arena.allocate<simd_hinge_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;
	const floatw one = 1.f;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_hinge_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}


		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);



		vec3w localAnchorA;
		vec3w localAnchorB;
		vec3w localHingeAxisA;
		vec3w localHingeAxisB;

		floatw minRotationLimit;
		floatw maxRotationLimit;

		floatw maxMotorTorque;
		floatw motorTypeF;



		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			localAnchorA.x, localAnchorA.y, localAnchorA.z,
			localAnchorB.x, localAnchorB.y, localAnchorB.z,
			localHingeAxisA.x, localHingeAxisA.y);

		load8(&input->localHingeAxisA.z, constraintIndices, (uint32)sizeof(*input),
			localHingeAxisA.z, 
			localHingeAxisB.x, localHingeAxisB.y, localHingeAxisB.z,
			minRotationLimit, maxRotationLimit,
			maxMotorTorque, motorTypeF);

		intw motorType = reinterpret(motorTypeF);


		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;




		// Position part. Identical to ball.

		mat3w skewMatA = getSkewMatrix(relGlobalAnchorA);
		mat3w skewMatB = getSkewMatrix(relGlobalAnchorB);

		mat3w invEffectiveTranslationMass = skewMatA * invInertiaA * transpose(skewMatA)
										  + skewMatB * invInertiaB * transpose(skewMatB);
		floatw invMassSum = invMassA + invMassB;
		invEffectiveTranslationMass.m00 += invMassSum;
		invEffectiveTranslationMass.m11 += invMassSum;
		invEffectiveTranslationMass.m22 += invMassSum;

		vec3w translationBias = zero;
		if (dt > DT_THRESHOLD)
		{
			translationBias = (globalAnchorB - globalAnchorA) * (floatw(BALL_CONSTRAINT_BETA) * invDt);
		}

		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		translationBias.x.store(batch.translationBias[0]);
		translationBias.y.store(batch.translationBias[1]);
		translationBias.z.store(batch.translationBias[2]);

		invEffectiveTranslationMass.m[0].store(batch.invEffectiveTranslationMass[0]);
		invEffectiveTranslationMass.m[1].store(batch.invEffectiveTranslationMass[1]);
		invEffectiveTranslationMass.m[2].store(batch.invEffectiveTranslationMass[2]);
		invEffectiveTranslationMass.m[3].store(batch.invEffectiveTranslationMass[3]);
		invEffectiveTranslationMass.m[4].store(batch.invEffectiveTranslationMass[4]);
		invEffectiveTranslationMass.m[5].store(batch.invEffectiveTranslationMass[5]);
		invEffectiveTranslationMass.m[6].store(batch.invEffectiveTranslationMass[6]);
		invEffectiveTranslationMass.m[7].store(batch.invEffectiveTranslationMass[7]);
		invEffectiveTranslationMass.m[8].store(batch.invEffectiveTranslationMass[8]);



		// Rotation part.
		vec3w globalHingeAxisA = rotationA * localHingeAxisA;
		vec3w globalHingeAxisB = rotationB * localHingeAxisB;

		vec3w globalTangentB, globalBitangentB;
		getTangents(globalHingeAxisB, globalTangentB, globalBitangentB);

		vec3w bxa = cross(globalTangentB, globalHingeAxisA);
		vec3w cxa = cross(globalBitangentB, globalHingeAxisA);
		vec3w iAbxa = invInertiaA * bxa;
		vec3w iBbxa = invInertiaB * bxa;
		vec3w iAcxa = invInertiaA * cxa;
		vec3w iBcxa = invInertiaB * cxa;

		mat2w invEffectiveRotationMass;
		invEffectiveRotationMass.m00 = dot(bxa, iAbxa) + dot(bxa, iBbxa);
		invEffectiveRotationMass.m01 = dot(bxa, iAcxa) + dot(bxa, iBcxa);
		invEffectiveRotationMass.m10 = dot(cxa, iAbxa) + dot(cxa, iBbxa);
		invEffectiveRotationMass.m11 = dot(cxa, iAcxa) + dot(cxa, iBcxa);

		vec2w rotationBias = zero;
		if (dt > DT_THRESHOLD)
		{
			rotationBias = vec2w(dot(globalHingeAxisA, globalTangentB), dot(globalHingeAxisA, globalBitangentB)) * (floatw(HINGE_ROTATION_CONSTRAINT_BETA) * invDt);
		}

		rotationBias.x.store(batch.rotationBias[0]);
		rotationBias.y.store(batch.rotationBias[1]);

		invEffectiveRotationMass.m[0].store(batch.invEffectiveRotationMass[0]);
		invEffectiveRotationMass.m[1].store(batch.invEffectiveRotationMass[1]);
		invEffectiveRotationMass.m[2].store(batch.invEffectiveRotationMass[2]);
		invEffectiveRotationMass.m[3].store(batch.invEffectiveRotationMass[3]);

		bxa.x.store(batch.bxa[0]);
		bxa.y.store(batch.bxa[1]);
		bxa.z.store(batch.bxa[2]);

		cxa.x.store(batch.cxa[0]);
		cxa.y.store(batch.cxa[1]);
		cxa.z.store(batch.cxa[2]);


		// Limits and motor.
		batch.solveLimit = false;
		batch.solveMotor = false;

		auto minLimitActive = minRotationLimit <= zero;
		auto maxLimitActive = maxRotationLimit >= zero;
		auto motorActive = maxMotorTorque > zero;
		auto cond = minLimitActive | maxLimitActive | motorActive;

		if (anyTrue(cond))
		{
			floatw motorVelocity;

			vec3w localHingeTangentA;
			vec3w localHingeBitangentA;
			vec3w localHingeTangentB;

			floatw dummy0, dummy1;

			load8(&input->motorVelocity, constraintIndices, (uint32)sizeof(*input),
				motorVelocity,
				localHingeTangentA.x, localHingeTangentA.y, localHingeTangentA.z,
				localHingeBitangentA.x, localHingeBitangentA.y, localHingeBitangentA.z,
				localHingeTangentB.x);

			load4(&input->localHingeTangentB.y, constraintIndices, (uint32)sizeof(*input),
				localHingeTangentB.y, localHingeTangentB.z,
				dummy0, dummy1);



			vec3w localHingeCompareA = conjugate(rotationA) * (rotationB * localHingeTangentB);
			floatw angle = atan2(dot(localHingeCompareA, localHingeBitangentA), dot(localHingeCompareA, localHingeTangentA));

			auto minLimitViolated = minLimitActive & (angle <= minRotationLimit);
			auto maxLimitViolated = maxLimitActive & (angle >= maxRotationLimit);

			auto limitViolated = minLimitViolated | maxLimitViolated;

			batch.solveLimit = anyTrue(limitViolated);
			batch.solveMotor = anyTrue(motorActive);
			if (batch.solveLimit || batch.solveMotor)
			{
				vec3w globalRotationAxis = globalHingeAxisA;

				floatw invEffectiveAxialMass = dot(globalRotationAxis, invInertiaA * globalRotationAxis)
											 + dot(globalRotationAxis, invInertiaB * globalRotationAxis);

				floatw effectiveAxialMass = ifThen(invEffectiveAxialMass != zero, 1.f / invEffectiveAxialMass, zero);
				floatw effectiveLimitAxialMass = ifThen(limitViolated, effectiveAxialMass, zero);
				floatw effectiveMotorAxialMass = ifThen(motorActive, effectiveAxialMass, zero);

				floatw limitSign = ifThen(minLimitViolated, 1.f, -1.f);

				floatw maxMotorImpulse = maxMotorTorque * floatw(dt);
				maxMotorImpulse = ifThen(motorActive, maxMotorImpulse, zero);

				vec3w motorAndLimitImpulseToAngularVelocityA = invInertiaA * globalRotationAxis;
				vec3w motorAndLimitImpulseToAngularVelocityB = invInertiaB * globalRotationAxis;

				if (batch.solveMotor)
				{
					auto isVelocityMotor = motorType == constraint_velocity_motor;

					if (anyFalse(isVelocityMotor))
					{
						// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
						// This will later get clamped to the maximum motor impulse.

						floatw motorTargetAngle = motorVelocity; // This is a union.

						floatw minLimit = ifThen(minLimitActive, minRotationLimit, -M_PI);
						floatw maxLimit = ifThen(maxLimitActive, maxRotationLimit, M_PI);
						floatw targetAngle = clamp(motorTargetAngle, minLimit, maxLimit);

						floatw motorVelocityOverride = (dt > DT_THRESHOLD) ? ((targetAngle - angle) * invDt) : zero;
						motorVelocity = ifThen(reinterpret(isVelocityMotor), motorVelocity, motorVelocityOverride);
					}
				}

				floatw limitBias = zero;
				if (batch.solveLimit && dt > DT_THRESHOLD)
				{
					floatw d = ifThen(minLimitViolated, angle - minRotationLimit, maxRotationLimit - angle);
					limitBias = d * floatw(HINGE_LIMIT_CONSTRAINT_BETA) * invDt;
				}
				
				globalRotationAxis.x.store(batch.globalRotationAxis[0]);
				globalRotationAxis.y.store(batch.globalRotationAxis[1]);
				globalRotationAxis.z.store(batch.globalRotationAxis[2]);

				effectiveLimitAxialMass.store(batch.effectiveLimitAxialMass);
				effectiveMotorAxialMass.store(batch.effectiveMotorAxialMass);

				zero.store(batch.limitImpulse);
				limitBias.store(batch.limitBias);
				limitSign.store(batch.limitSign);

				zero.store(batch.motorImpulse);
				maxMotorImpulse.store(batch.maxMotorImpulse);
				motorVelocity.store(batch.motorVelocity);

				motorAndLimitImpulseToAngularVelocityA.x.store(batch.motorAndLimitImpulseToAngularVelocityA[0]);
				motorAndLimitImpulseToAngularVelocityA.y.store(batch.motorAndLimitImpulseToAngularVelocityA[1]);
				motorAndLimitImpulseToAngularVelocityA.z.store(batch.motorAndLimitImpulseToAngularVelocityA[2]);

				motorAndLimitImpulseToAngularVelocityB.x.store(batch.motorAndLimitImpulseToAngularVelocityB[0]);
				motorAndLimitImpulseToAngularVelocityB.y.store(batch.motorAndLimitImpulseToAngularVelocityB[1]);
				motorAndLimitImpulseToAngularVelocityB.z.store(batch.motorAndLimitImpulseToAngularVelocityB[2]);
			}
		}

	}

	simd_hinge_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveHingeVelocityConstraintsSIMD(simd_hinge_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve hinge constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_hinge_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		mat3w invInertiaA;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21,
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		mat3w invInertiaB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Solve in order of importance (most important last): Motor -> Limits -> Rotation -> Position.

		vec3w globalRotationAxis(batch.globalRotationAxis[0], batch.globalRotationAxis[1], batch.globalRotationAxis[2]);
		vec3w motorAndLimitImpulseToAngularVelocityA(batch.motorAndLimitImpulseToAngularVelocityA[0], batch.motorAndLimitImpulseToAngularVelocityA[1], batch.motorAndLimitImpulseToAngularVelocityA[2]);
		vec3w motorAndLimitImpulseToAngularVelocityB(batch.motorAndLimitImpulseToAngularVelocityB[0], batch.motorAndLimitImpulseToAngularVelocityB[1], batch.motorAndLimitImpulseToAngularVelocityB[2]);

		// Motor.
		if (batch.solveMotor)
		{
			floatw motorVelocity(batch.motorVelocity);
			floatw effectiveMotorAxialMass(batch.effectiveMotorAxialMass);
			floatw motorImpulse(batch.motorImpulse);
			floatw maxMotorImpulse(batch.maxMotorImpulse);

			floatw aDotWA = dot(globalRotationAxis, wA); // How fast are we turning about the axis.
			floatw aDotWB = dot(globalRotationAxis, wB);

			floatw relAngularVelocity = (aDotWB - aDotWA);
			floatw motorCdot = relAngularVelocity - motorVelocity;

			floatw motorLambda = -effectiveMotorAxialMass * motorCdot;
			floatw oldImpulse = motorImpulse;
			motorImpulse = clamp(motorImpulse + motorLambda, -maxMotorImpulse, maxMotorImpulse);
			motorLambda = motorImpulse - oldImpulse;

			wA -= motorAndLimitImpulseToAngularVelocityA * motorLambda;
			wB += motorAndLimitImpulseToAngularVelocityB * motorLambda;

			motorImpulse.store(batch.motorImpulse);
		}

		// Limits.
		if (batch.solveLimit)
		{
			floatw limitSign(batch.limitSign);
			floatw limitBias(batch.limitBias);
			floatw limitImpulse(batch.limitImpulse);
			floatw effectiveLimitAxialMass(batch.effectiveLimitAxialMass);

			floatw aDotWA = dot(globalRotationAxis, wA); // How fast are we turning about the axis.
			floatw aDotWB = dot(globalRotationAxis, wB);
			floatw relAngularVelocity = limitSign * (aDotWB - aDotWA);

			floatw limitCdot = relAngularVelocity + limitBias;
			floatw limitLambda = -effectiveLimitAxialMass * limitCdot;

			floatw impulse = maximum(limitImpulse + limitLambda, 0.f);
			limitLambda = impulse - limitImpulse;
			limitImpulse = impulse;

			limitLambda *= limitSign;

			wA -= motorAndLimitImpulseToAngularVelocityA * limitLambda;
			wB += motorAndLimitImpulseToAngularVelocityB * limitLambda;

			limitImpulse.store(batch.limitImpulse);
		}

		// Rotation part.
		{
			vec3w bxa(batch.bxa[0], batch.bxa[1], batch.bxa[2]);
			vec3w cxa(batch.cxa[0], batch.cxa[1], batch.cxa[2]);
			vec2w rotationBias(batch.rotationBias[0], batch.rotationBias[1]);
			mat2w invEffectiveRotationMass;
			invEffectiveRotationMass.m[0] = floatw(batch.invEffectiveRotationMass[0]);
			invEffectiveRotationMass.m[1] = floatw(batch.invEffectiveRotationMass[1]);
			invEffectiveRotationMass.m[2] = floatw(batch.invEffectiveRotationMass[2]);
			invEffectiveRotationMass.m[3] = floatw(batch.invEffectiveRotationMass[3]);

			vec3w deltaAngularVelocity = wB - wA;

			vec2w rotationCdot(dot(bxa, deltaAngularVelocity), dot(cxa, deltaAngularVelocity));
			vec2w rotLambda = solveLinearSystem(invEffectiveRotationMass, -(rotationCdot + rotationBias));

			vec3w rotationP = bxa * rotLambda.x + cxa * rotLambda.y;

			wA -= invInertiaA * rotationP;
			wB += invInertiaB * rotationP;
		}

		// Position part.
		{
			vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
			vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
			vec3w translationBias(batch.translationBias[0], batch.translationBias[1], batch.translationBias[2]);

			mat3w invEffectiveTranslationMass;
			invEffectiveTranslationMass.m[0] = floatw(batch.invEffectiveTranslationMass[0]);
			invEffectiveTranslationMass.m[1] = floatw(batch.invEffectiveTranslationMass[1]);
			invEffectiveTranslationMass.m[2] = floatw(batch.invEffectiveTranslationMass[2]);
			invEffectiveTranslationMass.m[3] = floatw(batch.invEffectiveTranslationMass[3]);
			invEffectiveTranslationMass.m[4] = floatw(batch.invEffectiveTranslationMass[4]);
			invEffectiveTranslationMass.m[5] = floatw(batch.invEffectiveTranslationMass[5]);
			invEffectiveTranslationMass.m[6] = floatw(batch.invEffectiveTranslationMass[6]);
			invEffectiveTranslationMass.m[7] = floatw(batch.invEffectiveTranslationMass[7]);
			invEffectiveTranslationMass.m[8] = floatw(batch.invEffectiveTranslationMass[8]);

			vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
			vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);
			vec3w translationCdot = anchorVelocityB - anchorVelocityA + translationBias;

			vec3w translationP = solveLinearSystem(invEffectiveTranslationMass, -translationCdot);

			vA -= invMassA * translationP;
			wA -= invInertiaA * cross(relGlobalAnchorA, translationP);
			vB += invMassB * translationP;
			wB += invInertiaB * cross(relGlobalAnchorB, translationP);
		}


		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}




cone_twist_constraint_solver initializeConeTwistVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const cone_twist_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize cone twist constraints");

	float invDt = 1.f / dt;

	cone_twist_constraint_update* constraints = arena.allocate<cone_twist_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const cone_twist_constraint& in = input[i];
		cone_twist_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		out.relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		out.relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + out.relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + out.relGlobalAnchorB;

		mat3 skewMatA = getSkewMatrix(out.relGlobalAnchorA);
		mat3 skewMatB = getSkewMatrix(out.relGlobalAnchorB);

		out.invEffectiveMass = skewMatA * globalA.invInertia * transpose(skewMatA)
							 + skewMatB * globalB.invInertia * transpose(skewMatB)
							 + mat3::identity * (globalA.invMass + globalB.invMass);

		out.bias = 0.f;
		if (dt > DT_THRESHOLD)
		{
			out.bias = (globalAnchorB - globalAnchorA) * (BALL_CONSTRAINT_BETA * invDt);
		}


		// Limits and motors.

		quat btoa = conjugate(globalA.rotation) * globalB.rotation;

		vec3 localLimitAxisA = in.localLimitAxisA;
		vec3 localLimitAxisCompareA = btoa * in.localLimitAxisB;

		quat swingRotation = rotateFromTo(localLimitAxisA, localLimitAxisCompareA);

		vec3 twistTangentA = swingRotation * in.localLimitTangentA;
		vec3 twistBitangentA = swingRotation * in.localLimitBitangentA;
		vec3 localLimitTangentCompareA = btoa * in.localLimitTangentB;
		float twistAngle = atan2(dot(localLimitTangentCompareA, twistBitangentA), dot(localLimitTangentCompareA, twistTangentA));



		// Swing limit.
		vec3 swingAxis; float swingAngle;
		getAxisRotation(swingRotation, swingAxis, swingAngle);
		if (swingAngle < 0.f)
		{
			swingAngle *= -1.f;
			swingAxis *= -1.f;
		}

		out.solveSwingLimit = in.swingLimit >= 0.f && swingAngle >= in.swingLimit;
		if (out.solveSwingLimit)
		{
			out.swingImpulse = 0.f;
			out.globalSwingAxis = globalA.rotation * swingAxis;
			float invEffectiveLimitMass = dot(out.globalSwingAxis, globalA.invInertia * out.globalSwingAxis)
										+ dot(out.globalSwingAxis, globalB.invInertia * out.globalSwingAxis);
			out.effectiveSwingLimitMass = (invEffectiveLimitMass != 0.f) ? (1.f / invEffectiveLimitMass) : 0.f;

			out.swingLimitBias = 0.f;
			if (dt > DT_THRESHOLD)
			{
				out.swingLimitBias = (in.swingLimit - swingAngle) * (HINGE_LIMIT_CONSTRAINT_BETA * invDt);
			}

			out.swingLimitImpulseToAngularVelocityA = globalA.invInertia * out.globalSwingAxis;
			out.swingLimitImpulseToAngularVelocityB = globalB.invInertia * out.globalSwingAxis;
		}

		// Swing motor.
		out.solveSwingMotor = in.maxSwingMotorTorque > 0.f;
		if (out.solveSwingMotor)
		{
			out.maxSwingMotorImpulse = in.maxSwingMotorTorque * dt;
			out.swingMotorImpulse = 0.f;

			float axisX = cos(in.swingMotorAxis), axisY = sin(in.swingMotorAxis);
			vec3 localSwingMotorAxis = axisX * in.localLimitTangentA + axisY * in.localLimitBitangentA;

			if (in.swingMotorType == constraint_velocity_motor)
			{
				out.globalSwingMotorAxis = globalA.rotation * localSwingMotorAxis;
				out.swingMotorVelocity = in.swingMotorVelocity;
			}
			else
			{
				float targetAngle = in.swingMotorTargetAngle;
				if (in.swingLimit >= 0.f)
				{
					targetAngle = clamp(targetAngle, -in.swingLimit, in.swingLimit);
				}

				vec3 localTargetDirection = quat(localSwingMotorAxis, targetAngle) * localLimitAxisA;
				vec3 localSwingMotorAxis = noz(cross(localLimitAxisCompareA, localTargetDirection));
				out.globalSwingMotorAxis = globalA.rotation * localSwingMotorAxis;

				float cosAngle = dot(localTargetDirection, localLimitAxisCompareA);
				float deltaAngle = acos(clamp01(cosAngle));
				out.swingMotorVelocity = (dt > DT_THRESHOLD) ? (deltaAngle * invDt * 0.2f) : 0.f;
			}

			out.swingMotorImpulseToAngularVelocityA = globalA.invInertia * out.globalSwingMotorAxis;
			out.swingMotorImpulseToAngularVelocityB = globalB.invInertia * out.globalSwingMotorAxis;

			float invEffectiveMotorMass = dot(out.globalSwingMotorAxis, globalA.invInertia * out.globalSwingMotorAxis)
										+ dot(out.globalSwingMotorAxis, globalB.invInertia * out.globalSwingMotorAxis);
			out.effectiveSwingMotorMass = (invEffectiveMotorMass != 0.f) ? (1.f / invEffectiveMotorMass) : 0.f;
		}

		// Twist limit and motor.
		bool minTwistLimitViolated = in.twistLimit >= 0.f && twistAngle <= -in.twistLimit;
		bool maxTwistLimitViolated = in.twistLimit >= 0.f && twistAngle >= in.twistLimit; 
		assert(!(minTwistLimitViolated && maxTwistLimitViolated));

		out.solveTwistLimit = minTwistLimitViolated || maxTwistLimitViolated;
		out.solveTwistMotor = in.maxTwistMotorTorque > 0.f;
		if (out.solveTwistLimit || out.solveTwistMotor)
		{
			out.twistImpulse = 0.f;
			out.globalTwistAxis = globalA.rotation * localLimitAxisA;
			float invEffectiveMass = dot(out.globalTwistAxis, globalA.invInertia * out.globalTwistAxis)
								   + dot(out.globalTwistAxis, globalB.invInertia * out.globalTwistAxis);
			out.effectiveTwistMass = (invEffectiveMass != 0.f) ? (1.f / invEffectiveMass) : 0.f;

			out.twistLimitSign = minTwistLimitViolated ? 1.f : -1.f;

			out.maxTwistMotorImpulse = in.maxTwistMotorTorque * dt;
			out.twistMotorImpulse = 0.f;

			out.twistMotorAndLimitImpulseToAngularVelocityA = globalA.invInertia * out.globalTwistAxis;
			out.twistMotorAndLimitImpulseToAngularVelocityB = globalB.invInertia * out.globalTwistAxis;

			out.twistMotorVelocity = in.twistMotorVelocity;
			if (in.twistMotorType == constraint_position_motor)
			{
				// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
				// This will later get clamped to the maximum motor impulse.
				float limit = (in.twistLimit >= 0.f) ? in.twistLimit : M_PI;
				float targetAngle = clamp(in.twistMotorTargetAngle, -limit, limit);
				out.twistMotorVelocity = (dt > DT_THRESHOLD) ? ((targetAngle - twistAngle) * invDt) : 0.f;
			}

			out.twistLimitBias = 0.f;
			if (dt > DT_THRESHOLD)
			{
				float d = minTwistLimitViolated ? (in.twistLimit + twistAngle) : (in.twistLimit - twistAngle);
				out.twistLimitBias = d * TWIST_LIMIT_CONSTRAINT_BETA * invDt;
			}
		}
	}

	cone_twist_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveConeTwistVelocityConstraints(cone_twist_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve cone twist constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		cone_twist_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		vec3 vA = rbA.linearVelocity;
		vec3 wA = rbA.angularVelocity;
		vec3 vB = rbB.linearVelocity;
		vec3 wB = rbB.angularVelocity;

		// Solve in order of importance (most important last): Motors -> Limits -> Position.

		vec3 globalTwistAxis = con.globalTwistAxis;

		// Motor.
		if (con.solveTwistMotor)
		{
			float aDotWA = dot(globalTwistAxis, wA);
			float aDotWB = dot(globalTwistAxis, wB);
			float relAngularVelocity = (aDotWB - aDotWA);

			float motorCdot = relAngularVelocity - con.twistMotorVelocity;

			float motorLambda = -con.effectiveTwistMass * motorCdot;
			float oldImpulse = con.twistMotorImpulse;
			con.twistMotorImpulse = clamp(con.twistMotorImpulse + motorLambda, -con.maxTwistMotorImpulse, con.maxTwistMotorImpulse);
			motorLambda = con.twistMotorImpulse - oldImpulse;

			wA -= con.twistMotorAndLimitImpulseToAngularVelocityA * motorLambda;
			wB += con.twistMotorAndLimitImpulseToAngularVelocityB * motorLambda;
		}

		if (con.solveSwingMotor)
		{
			vec3 globalSwingMotorAxis = con.globalSwingMotorAxis;

			float aDotWA = dot(globalSwingMotorAxis, wA);
			float aDotWB = dot(globalSwingMotorAxis, wB);
			float relAngularVelocity = (aDotWB - aDotWA);

			float motorCdot = relAngularVelocity - con.swingMotorVelocity;

			float motorLambda = -con.effectiveSwingMotorMass * motorCdot;
			float oldImpulse = con.swingMotorImpulse;
			con.swingMotorImpulse = clamp(con.swingMotorImpulse + motorLambda, -con.maxSwingMotorImpulse, con.maxSwingMotorImpulse);
			motorLambda = con.swingMotorImpulse - oldImpulse;

			wA -= con.swingMotorImpulseToAngularVelocityA * motorLambda;
			wB += con.swingMotorImpulseToAngularVelocityB * motorLambda;
		}

		// Twist.
		if (con.solveTwistLimit)
		{
			float limitSign = con.twistLimitSign;

			float aDotWA = dot(globalTwistAxis, wA);
			float aDotWB = dot(globalTwistAxis, wB);
			float relAngularVelocity = limitSign * (aDotWB - aDotWA);

			float limitCdot = relAngularVelocity + con.twistLimitBias;
			float limitLambda = -con.effectiveTwistMass * limitCdot;

			float impulse = max(con.twistImpulse + limitLambda, 0.f);
			limitLambda = impulse - con.twistImpulse;
			con.twistImpulse = impulse;

			limitLambda *= limitSign;

			wA -= con.twistMotorAndLimitImpulseToAngularVelocityA * limitLambda;
			wB += con.twistMotorAndLimitImpulseToAngularVelocityB * limitLambda;
		}

		// Cone.
		if (con.solveSwingLimit)
		{
			float aDotWA = dot(con.globalSwingAxis, wA);
			float aDotWB = dot(con.globalSwingAxis, wB);
			float swingLimitCdot = aDotWA - aDotWB + con.swingLimitBias;
			float limitLambda = -con.effectiveSwingLimitMass * swingLimitCdot;

			float impulse = max(con.swingImpulse + limitLambda, 0.f);
			limitLambda = impulse - con.swingImpulse;
			con.swingImpulse = impulse;

			wA += con.swingLimitImpulseToAngularVelocityA * limitLambda;
			wB -= con.swingLimitImpulseToAngularVelocityB * limitLambda;
		}


		// Position part.
		{
			vec3 anchorVelocityA = vA + cross(wA, con.relGlobalAnchorA);
			vec3 anchorVelocityB = vB + cross(wB, con.relGlobalAnchorB);
			vec3 translationCdot = anchorVelocityB - anchorVelocityA + con.bias;

			vec3 translationP = solveLinearSystem(con.invEffectiveMass, -translationCdot);

			vA -= rbA.invMass * translationP;
			wA -= rbA.invInertia * cross(con.relGlobalAnchorA, translationP);
			vB += rbB.invMass * translationP;
			wB += rbB.invInertia * cross(con.relGlobalAnchorB, translationP);
		}

		rbA.linearVelocity = vA;
		rbA.angularVelocity = wA;
		rbB.linearVelocity = vB;
		rbB.angularVelocity = wB;
	}
}

simd_cone_twist_constraint_solver initializeConeTwistVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const cone_twist_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize cone twist constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_cone_twist_constraint_batch* batches = arena.allocate<simd_cone_twist_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;
	const floatw one = 1.f;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_cone_twist_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}


		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);






		vec3w localAnchorA;
		vec3w localAnchorB;

		vec3w localLimitAxisA;
		vec3w localLimitAxisB;

		vec3w localLimitTangentA;
		vec3w localLimitBitangentA;
		vec3w localLimitTangentB;

		floatw swingLimit;
		floatw twistLimit;

		floatw swingMotorTypeF;
		floatw swingMotorVelocity;
		floatw maxSwingMotorTorque;
		floatw swingMotorAxis;

		floatw twistMotorTypeF;
		floatw twistMotorVelocity;
		floatw maxTwistMotorTorque;

		floatw dummy0, dummy1;


		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			localAnchorA.x, localAnchorA.y, localAnchorA.z, 
			localAnchorB.x, localAnchorB.y, localAnchorB.z, 
			localLimitAxisA.x, localLimitAxisA.y);

		load8(&input->localLimitAxisA.z, constraintIndices, (uint32)sizeof(*input),
			localLimitAxisA.z,
			localLimitAxisB.x, localLimitAxisB.y, localLimitAxisB.z,
			localLimitTangentA.x, localLimitTangentA.y, localLimitTangentA.z, 
			localLimitBitangentA.x);

		load8(&input->localLimitBitangentA.y, constraintIndices, (uint32)sizeof(*input),
			localLimitBitangentA.y, localLimitBitangentA.z,
			localLimitTangentB.x, localLimitTangentB.y, localLimitTangentB.z,
			swingLimit, twistLimit, swingMotorTypeF);

		load8(&input->swingMotorVelocity, constraintIndices, (uint32)sizeof(*input),
			swingMotorVelocity, maxSwingMotorTorque, swingMotorAxis,
			twistMotorTypeF, twistMotorVelocity, maxTwistMotorTorque,
			dummy0, dummy1);


		intw swingMotorType = reinterpret(swingMotorTypeF);
		intw twistMotorType = reinterpret(twistMotorTypeF);






		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;

		mat3w skewMatA = getSkewMatrix(relGlobalAnchorA);
		mat3w skewMatB = getSkewMatrix(relGlobalAnchorB);

		mat3w invEffectiveMass = skewMatA * invInertiaA * transpose(skewMatA)
							   + skewMatB * invInertiaB * transpose(skewMatB);
		floatw invMassSum = invMassA + invMassB;
		invEffectiveMass.m00 += invMassSum;
		invEffectiveMass.m11 += invMassSum;
		invEffectiveMass.m22 += invMassSum;

		vec3w bias = zero;
		if (dt > DT_THRESHOLD)
		{
			bias = (globalAnchorB - globalAnchorA) * (floatw(BALL_CONSTRAINT_BETA) * invDt);
		}

		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		bias.x.store(batch.bias[0]);
		bias.y.store(batch.bias[1]);
		bias.z.store(batch.bias[2]);

		invEffectiveMass.m[0].store(batch.invEffectiveMass[0]);
		invEffectiveMass.m[1].store(batch.invEffectiveMass[1]);
		invEffectiveMass.m[2].store(batch.invEffectiveMass[2]);
		invEffectiveMass.m[3].store(batch.invEffectiveMass[3]);
		invEffectiveMass.m[4].store(batch.invEffectiveMass[4]);
		invEffectiveMass.m[5].store(batch.invEffectiveMass[5]);
		invEffectiveMass.m[6].store(batch.invEffectiveMass[6]);
		invEffectiveMass.m[7].store(batch.invEffectiveMass[7]);
		invEffectiveMass.m[8].store(batch.invEffectiveMass[8]);


		// Limits and motors.

		quatw btoa = conjugate(rotationA) * rotationB;

		vec3w localLimitAxisCompareA = btoa * localLimitAxisB;

		quatw swingRotation = rotateFromTo(localLimitAxisA, localLimitAxisCompareA);

		vec3w twistTangentA = swingRotation * localLimitTangentA;
		vec3w twistBitangentA = swingRotation * localLimitBitangentA;
		vec3w localLimitTangentCompareA = btoa * localLimitTangentB;
		floatw twistAngle = atan2(dot(localLimitTangentCompareA, twistBitangentA), dot(localLimitTangentCompareA, twistTangentA));



		// Swing limit.
		vec3w swingAxis; floatw swingAngle;
		getAxisRotation(swingRotation, swingAxis, swingAngle);
		auto swingAngleNegative = swingAngle < zero;
		swingAngle = ifThen(swingAngleNegative, -swingAngle, swingAngle);
		swingAxis = ifThen(swingAngleNegative, -swingAxis, swingAxis);

		auto solveSwingLimit = (swingLimit >= zero) & (swingAngle >= swingLimit);
		batch.solveSwingLimit = anyTrue(solveSwingLimit);
		if (batch.solveSwingLimit)
		{
			vec3w globalSwingAxis = rotationA * swingAxis;
			floatw invEffectiveLimitMass = dot(globalSwingAxis, invInertiaA * globalSwingAxis)
										 + dot(globalSwingAxis, invInertiaB * globalSwingAxis);
			floatw effectiveSwingLimitMass = ifThen(invEffectiveLimitMass != zero, 1.f / invEffectiveLimitMass, zero);
			effectiveSwingLimitMass = ifThen(solveSwingLimit, effectiveSwingLimitMass, zero); // Set to zero for constraints, which aren't at the limit.

			floatw swingLimitBias = zero;
			if (dt > DT_THRESHOLD)
			{
				swingLimitBias = (swingLimit - swingAngle) * (floatw(HINGE_LIMIT_CONSTRAINT_BETA) * invDt);
			}

			vec3w swingLimitImpulseToAngularVelocityA = invInertiaA * globalSwingAxis;
			vec3w swingLimitImpulseToAngularVelocityB = invInertiaB * globalSwingAxis;

			globalSwingAxis.x.store(batch.globalSwingAxis[0]);
			globalSwingAxis.y.store(batch.globalSwingAxis[1]);
			globalSwingAxis.z.store(batch.globalSwingAxis[2]);

			zero.store(batch.swingImpulse);
			effectiveSwingLimitMass.store(batch.effectiveSwingLimitMass);
			swingLimitBias.store(batch.swingLimitBias);

			swingLimitImpulseToAngularVelocityA.x.store(batch.swingLimitImpulseToAngularVelocityA[0]);
			swingLimitImpulseToAngularVelocityA.y.store(batch.swingLimitImpulseToAngularVelocityA[1]);
			swingLimitImpulseToAngularVelocityA.z.store(batch.swingLimitImpulseToAngularVelocityA[2]);

			swingLimitImpulseToAngularVelocityB.x.store(batch.swingLimitImpulseToAngularVelocityB[0]);
			swingLimitImpulseToAngularVelocityB.y.store(batch.swingLimitImpulseToAngularVelocityB[1]);
			swingLimitImpulseToAngularVelocityB.z.store(batch.swingLimitImpulseToAngularVelocityB[2]);
		}

		// Swing motor.
		auto solveSwingMotor = maxSwingMotorTorque > zero;
		batch.solveSwingMotor = anyTrue(solveSwingMotor);
		if (batch.solveSwingMotor)
		{
			floatw maxSwingMotorImpulse = maxSwingMotorTorque * floatw(dt);
			maxSwingMotorTorque = ifThen(solveSwingMotor, maxSwingMotorTorque, zero);

			floatw axisX = cos(swingMotorAxis), axisY = sin(swingMotorAxis);
			vec3w localSwingMotorAxis = axisX * localLimitTangentA + axisY * localLimitBitangentA;

			auto isVelocityMotor = swingMotorType == constraint_velocity_motor;
			vec3w globalSwingMotorAxis = rotationA * localSwingMotorAxis;

			if (anyFalse(isVelocityMotor)) // At least one position motor.
			{
				floatw targetAngle = swingMotorVelocity; // This is a union.
				targetAngle = ifThen(swingLimit >= zero, clamp(targetAngle, -swingLimit, swingLimit), targetAngle);

				vec3w localTargetDirection = quatw(localSwingMotorAxis, targetAngle) * localLimitAxisA;
				vec3w localSwingMotorAxis = noz(cross(localLimitAxisCompareA, localTargetDirection));
				vec3w globalSwingMotorAxisOverride = rotationA * localSwingMotorAxis;

				floatw cosAngle = dot(localTargetDirection, localLimitAxisCompareA);
				floatw deltaAngle = acos(clamp01(cosAngle));
				floatw swingMotorVelocityOverride = (dt > DT_THRESHOLD) ? (deltaAngle * invDt * floatw(0.2f)) : zero;

				swingMotorVelocity = ifThen(reinterpret(isVelocityMotor), swingMotorVelocity, swingMotorVelocityOverride);
				globalSwingMotorAxis = ifThen(reinterpret(isVelocityMotor), globalSwingMotorAxis, globalSwingMotorAxisOverride);
			}

			vec3w swingMotorImpulseToAngularVelocityA = invInertiaA * globalSwingMotorAxis;
			vec3w swingMotorImpulseToAngularVelocityB = invInertiaB * globalSwingMotorAxis;

			floatw invEffectiveMotorMass = dot(globalSwingMotorAxis, invInertiaA * globalSwingMotorAxis)
										 + dot(globalSwingMotorAxis, invInertiaB * globalSwingMotorAxis);
			floatw effectiveSwingMotorMass = ifThen(invEffectiveMotorMass != zero, 1.f / invEffectiveMotorMass, zero);
			effectiveSwingMotorMass = ifThen(solveSwingMotor, effectiveSwingMotorMass, zero); // Set to zero for constraints, which have no motor.

			
			zero.store(batch.swingMotorImpulse);
			maxSwingMotorImpulse.store(batch.maxSwingMotorImpulse);
			swingMotorVelocity.store(batch.swingMotorVelocity);

			globalSwingMotorAxis.x.store(batch.globalSwingMotorAxis[0]);
			globalSwingMotorAxis.y.store(batch.globalSwingMotorAxis[1]);
			globalSwingMotorAxis.z.store(batch.globalSwingMotorAxis[2]);

			swingMotorImpulseToAngularVelocityA.x.store(batch.swingMotorImpulseToAngularVelocityA[0]);
			swingMotorImpulseToAngularVelocityA.y.store(batch.swingMotorImpulseToAngularVelocityA[1]);
			swingMotorImpulseToAngularVelocityA.z.store(batch.swingMotorImpulseToAngularVelocityA[2]);

			swingMotorImpulseToAngularVelocityB.x.store(batch.swingMotorImpulseToAngularVelocityB[0]);
			swingMotorImpulseToAngularVelocityB.y.store(batch.swingMotorImpulseToAngularVelocityB[1]);
			swingMotorImpulseToAngularVelocityB.z.store(batch.swingMotorImpulseToAngularVelocityB[2]);

			effectiveSwingMotorMass.store(batch.effectiveSwingMotorMass);
		}

		// Twist limit and motor.
		auto minTwistLimitViolated = (twistLimit >= zero) & (twistAngle <= -twistLimit);
		auto maxTwistLimitViolated = (twistLimit >= zero) & (twistAngle >= twistLimit);

		auto solveTwistLimit = minTwistLimitViolated | maxTwistLimitViolated;
		auto solveTwistMotor = maxTwistMotorTorque > zero;
		batch.solveTwistLimit = anyTrue(solveTwistLimit);
		batch.solveTwistMotor = anyTrue(solveTwistMotor);
		if (batch.solveTwistLimit || batch.solveTwistMotor)
		{
			vec3w globalTwistAxis = rotationA * localLimitAxisA;
			floatw invEffectiveMass = dot(globalTwistAxis, invInertiaA * globalTwistAxis)
									+ dot(globalTwistAxis, invInertiaB * globalTwistAxis);
			floatw effectiveTwistMass = ifThen(invEffectiveMass != zero, 1.f / invEffectiveMass, zero);

			floatw effectiveTwistLimitMass = ifThen(solveTwistLimit, effectiveTwistMass, zero); // Set to zero for constraints, which aren't at the limit.
			floatw effectiveTwistMotorMass = ifThen(solveTwistMotor, effectiveTwistMass, zero); // Set to zero for constraints, which have no motor.

			floatw twistLimitSign = ifThen(minTwistLimitViolated, 1.f, -1.f);

			floatw maxTwistMotorImpulse = maxTwistMotorTorque * floatw(dt);
			maxTwistMotorImpulse = ifThen(solveTwistMotor, maxTwistMotorImpulse, zero);

			vec3w twistMotorAndLimitImpulseToAngularVelocityA = invInertiaA * globalTwistAxis;
			vec3w twistMotorAndLimitImpulseToAngularVelocityB = invInertiaB * globalTwistAxis;

			if (batch.solveTwistMotor)
			{
				auto isVelocityMotor = twistMotorType == constraint_velocity_motor;

				if (anyFalse(isVelocityMotor)) // At least one position motor.
				{
					// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
					// This will later get clamped to the maximum motor impulse.
					floatw limit = ifThen(twistLimit >= zero, twistLimit, M_PI);
					floatw twistMotorTargetAngle = twistMotorVelocity; // This is a union.
					floatw targetAngle = clamp(twistMotorTargetAngle, -limit, limit);

					floatw twistMotorVelocityOverride = (dt > DT_THRESHOLD) ? ((targetAngle - twistAngle) * invDt) : zero;
					twistMotorVelocity = ifThen(reinterpret(isVelocityMotor), twistMotorVelocity, twistMotorVelocityOverride);
				}
			}

			floatw twistLimitBias = zero;
			if (batch.solveTwistLimit && dt > DT_THRESHOLD)
			{
				floatw d = ifThen(minTwistLimitViolated, twistLimit + twistAngle, twistLimit - twistAngle);
				twistLimitBias = d * (floatw(TWIST_LIMIT_CONSTRAINT_BETA) * invDt);
			}

			globalTwistAxis.x.store(batch.globalTwistAxis[0]);
			globalTwistAxis.y.store(batch.globalTwistAxis[1]);
			globalTwistAxis.z.store(batch.globalTwistAxis[2]);

			zero.store(batch.twistImpulse);
			twistLimitSign.store(batch.twistLimitSign);
			effectiveTwistLimitMass.store(batch.effectiveTwistLimitMass);
			effectiveTwistMotorMass.store(batch.effectiveTwistMotorMass);
			twistLimitBias.store(batch.twistLimitBias);

			maxTwistMotorImpulse.store(batch.maxTwistMotorImpulse);
			zero.store(batch.twistMotorImpulse);
			twistMotorVelocity.store(batch.twistMotorVelocity);

			twistMotorAndLimitImpulseToAngularVelocityA.x.store(batch.twistMotorAndLimitImpulseToAngularVelocityA[0]);
			twistMotorAndLimitImpulseToAngularVelocityA.y.store(batch.twistMotorAndLimitImpulseToAngularVelocityA[1]);
			twistMotorAndLimitImpulseToAngularVelocityA.z.store(batch.twistMotorAndLimitImpulseToAngularVelocityA[2]);

			twistMotorAndLimitImpulseToAngularVelocityB.x.store(batch.twistMotorAndLimitImpulseToAngularVelocityB[0]);
			twistMotorAndLimitImpulseToAngularVelocityB.y.store(batch.twistMotorAndLimitImpulseToAngularVelocityB[1]);
			twistMotorAndLimitImpulseToAngularVelocityB.z.store(batch.twistMotorAndLimitImpulseToAngularVelocityB[2]);
		}

	}

	simd_cone_twist_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveConeTwistVelocityConstraintsSIMD(simd_cone_twist_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve cone twist constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_cone_twist_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		mat3w invInertiaA;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21,
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		mat3w invInertiaB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Solve in order of importance (most important last): Motors -> Limits -> Position.

		vec3w globalTwistAxis(batch.globalTwistAxis[0], batch.globalTwistAxis[1], batch.globalTwistAxis[2]);
		vec3w twistMotorAndLimitImpulseToAngularVelocityA(batch.twistMotorAndLimitImpulseToAngularVelocityA[0], batch.twistMotorAndLimitImpulseToAngularVelocityA[1], batch.twistMotorAndLimitImpulseToAngularVelocityA[2]);
		vec3w twistMotorAndLimitImpulseToAngularVelocityB(batch.twistMotorAndLimitImpulseToAngularVelocityB[0], batch.twistMotorAndLimitImpulseToAngularVelocityB[1], batch.twistMotorAndLimitImpulseToAngularVelocityB[2]);

		// Motor.
		if (batch.solveTwistMotor)
		{
			floatw twistMotorVelocity(batch.twistMotorVelocity);
			floatw effectiveTwistMotorMass(batch.effectiveTwistMotorMass);
			floatw twistMotorImpulse(batch.twistMotorImpulse);
			floatw maxTwistMotorImpulse(batch.maxTwistMotorImpulse);

			floatw aDotWA = dot(globalTwistAxis, wA);
			floatw aDotWB = dot(globalTwistAxis, wB);
			floatw relAngularVelocity = (aDotWB - aDotWA);

			floatw motorCdot = relAngularVelocity - twistMotorVelocity;

			floatw motorLambda = -effectiveTwistMotorMass * motorCdot;
			floatw oldImpulse = twistMotorImpulse;
			twistMotorImpulse = clamp(twistMotorImpulse + motorLambda, -maxTwistMotorImpulse, maxTwistMotorImpulse);
			motorLambda = twistMotorImpulse - oldImpulse;

			wA -= twistMotorAndLimitImpulseToAngularVelocityA * motorLambda;
			wB += twistMotorAndLimitImpulseToAngularVelocityB * motorLambda;

			twistMotorImpulse.store(batch.twistMotorImpulse);
		}

		if (batch.solveSwingMotor)
		{
			floatw swingMotorVelocity(batch.swingMotorVelocity);
			floatw effectiveSwingMotorMass(batch.effectiveSwingMotorMass);
			vec3w globalSwingMotorAxis(batch.globalSwingMotorAxis[0], batch.globalSwingMotorAxis[1], batch.globalSwingMotorAxis[2]);
			floatw swingMotorImpulse(batch.swingMotorImpulse);
			floatw maxSwingMotorImpulse(batch.maxSwingMotorImpulse);

			vec3w swingMotorImpulseToAngularVelocityA(batch.swingMotorImpulseToAngularVelocityA[0], batch.swingMotorImpulseToAngularVelocityA[1], batch.swingMotorImpulseToAngularVelocityA[2]);
			vec3w swingMotorImpulseToAngularVelocityB(batch.swingMotorImpulseToAngularVelocityB[0], batch.swingMotorImpulseToAngularVelocityB[1], batch.swingMotorImpulseToAngularVelocityB[2]);

			floatw aDotWA = dot(globalSwingMotorAxis, wA);
			floatw aDotWB = dot(globalSwingMotorAxis, wB);
			floatw relAngularVelocity = (aDotWB - aDotWA);

			floatw motorCdot = relAngularVelocity - swingMotorVelocity;

			floatw motorLambda = -effectiveSwingMotorMass * motorCdot;
			floatw oldImpulse = swingMotorImpulse;
			swingMotorImpulse = clamp(swingMotorImpulse + motorLambda, -maxSwingMotorImpulse, maxSwingMotorImpulse);
			motorLambda = swingMotorImpulse - oldImpulse;

			wA -= swingMotorImpulseToAngularVelocityA * motorLambda;
			wB += swingMotorImpulseToAngularVelocityB * motorLambda;

			swingMotorImpulse.store(batch.swingMotorImpulse);
		}

		// Twist.
		if (batch.solveTwistLimit)
		{
			floatw limitSign(batch.twistLimitSign);
			floatw twistLimitBias(batch.twistLimitBias);
			floatw effectiveTwistLimitMass(batch.effectiveTwistLimitMass);
			floatw twistImpulse(batch.twistImpulse);

			floatw aDotWA = dot(globalTwistAxis, wA);
			floatw aDotWB = dot(globalTwistAxis, wB);
			floatw relAngularVelocity = limitSign * (aDotWB - aDotWA);

			floatw limitCdot = relAngularVelocity + twistLimitBias;
			floatw limitLambda = -effectiveTwistLimitMass * limitCdot;

			floatw impulse = maximum(twistImpulse + limitLambda, floatw::zero());
			limitLambda = impulse - twistImpulse;
			twistImpulse = impulse;

			limitLambda *= limitSign;

			wA -= twistMotorAndLimitImpulseToAngularVelocityA * limitLambda;
			wB += twistMotorAndLimitImpulseToAngularVelocityB * limitLambda;

			twistImpulse.store(batch.twistImpulse);
		}

		// Cone.
		if (batch.solveSwingLimit)
		{
			vec3w globalSwingAxis(batch.globalSwingAxis[0], batch.globalSwingAxis[1], batch.globalSwingAxis[2]);
			floatw swingLimitBias(batch.swingLimitBias);
			floatw effectiveSwingLimitMass(batch.effectiveSwingLimitMass);
			floatw swingImpulse(batch.swingImpulse);

			vec3w swingLimitImpulseToAngularVelocityA(batch.swingLimitImpulseToAngularVelocityA[0], batch.swingLimitImpulseToAngularVelocityA[1], batch.swingLimitImpulseToAngularVelocityA[2]);
			vec3w swingLimitImpulseToAngularVelocityB(batch.swingLimitImpulseToAngularVelocityB[0], batch.swingLimitImpulseToAngularVelocityB[1], batch.swingLimitImpulseToAngularVelocityB[2]);

			floatw aDotWA = dot(globalSwingAxis, wA);
			floatw aDotWB = dot(globalSwingAxis, wB);
			floatw swingLimitCdot = aDotWA - aDotWB + swingLimitBias;
			floatw limitLambda = -effectiveSwingLimitMass * swingLimitCdot;

			floatw impulse = maximum(swingImpulse + limitLambda, floatw::zero());
			limitLambda = impulse - swingImpulse;
			swingImpulse = impulse;

			wA += swingLimitImpulseToAngularVelocityA * limitLambda;
			wB -= swingLimitImpulseToAngularVelocityB * limitLambda;

			swingImpulse.store(batch.swingImpulse);
		}


		// Position part.
		{
			vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
			vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
			vec3w bias(batch.bias[0], batch.bias[1], batch.bias[2]);

			mat3w invEffectiveMass;
			invEffectiveMass.m[0] = floatw(batch.invEffectiveMass[0]);
			invEffectiveMass.m[1] = floatw(batch.invEffectiveMass[1]);
			invEffectiveMass.m[2] = floatw(batch.invEffectiveMass[2]);
			invEffectiveMass.m[3] = floatw(batch.invEffectiveMass[3]);
			invEffectiveMass.m[4] = floatw(batch.invEffectiveMass[4]);
			invEffectiveMass.m[5] = floatw(batch.invEffectiveMass[5]);
			invEffectiveMass.m[6] = floatw(batch.invEffectiveMass[6]);
			invEffectiveMass.m[7] = floatw(batch.invEffectiveMass[7]);
			invEffectiveMass.m[8] = floatw(batch.invEffectiveMass[8]);

			vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
			vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);
			vec3w translationCdot = anchorVelocityB - anchorVelocityA + bias;

			vec3w translationP = solveLinearSystem(invEffectiveMass, -translationCdot);

			vA -= invMassA * translationP;
			wA -= invInertiaA * cross(relGlobalAnchorA, translationP);
			vB += invMassB * translationP;
			wB += invInertiaB * cross(relGlobalAnchorB, translationP);
		}



		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}



slider_constraint_solver initializeSliderVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const slider_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize slider constraints");

	float invDt = 1.f / dt;

	slider_constraint_update* constraints = arena.allocate<slider_constraint_update>(count);

	for (uint32 i = 0; i < count; ++i)
	{
		const slider_constraint& in = input[i];
		slider_constraint_update& out = constraints[i];

		out.rigidBodyIndexA = bodyPairs[i].rbA;
		out.rigidBodyIndexB = bodyPairs[i].rbB;

		const rigid_body_global_state& globalA = rbs[out.rigidBodyIndexA];
		const rigid_body_global_state& globalB = rbs[out.rigidBodyIndexB];

		// Relative to COG.
		vec3 relGlobalAnchorA = globalA.rotation * (in.localAnchorA - globalA.localCOGPosition);
		vec3 relGlobalAnchorB = globalB.rotation * (in.localAnchorB - globalB.localCOGPosition);

		// Global.
		vec3 globalAnchorA = globalA.position + relGlobalAnchorA;
		vec3 globalAnchorB = globalB.position + relGlobalAnchorB;

		
		vec3 globalSliderAxis = globalA.rotation * in.localAxisA;
		getTangents(globalSliderAxis, out.tangent, out.bitangent);
		vec3 u = globalAnchorB - globalAnchorA;

		vec3 rAu = relGlobalAnchorA + u;
		out.rBxt = cross(relGlobalAnchorB, out.tangent);
		out.rBxb = cross(relGlobalAnchorB, out.bitangent);
		out.rAuxt = cross(rAu, out.tangent);
		out.rAuxb = cross(rAu, out.bitangent);

		vec3 iArAuxt = globalA.invInertia * out.rAuxt;
		vec3 iArAuxb = globalA.invInertia * out.rAuxb;
		vec3 iBrBxt = globalB.invInertia * out.rBxt;
		vec3 iBrBxb = globalB.invInertia * out.rBxb;
		float invMassSum = globalA.invMass + globalB.invMass;

		out.invEffectiveTranslationMass.m00 = dot(out.rAuxt, iArAuxt) + dot(out.rBxt, iBrBxt) + invMassSum;
		out.invEffectiveTranslationMass.m01 = dot(out.rAuxt, iArAuxb) + dot(out.rBxt, iBrBxb);
		out.invEffectiveTranslationMass.m10 = dot(out.rAuxb, iArAuxt) + dot(out.rBxb, iBrBxt);
		out.invEffectiveTranslationMass.m11 = dot(out.rAuxb, iArAuxb) + dot(out.rBxb, iBrBxb) + invMassSum;

		out.invEffectiveRotationMass = globalA.invInertia + globalB.invInertia;
		out.translationBias = vec2(0.f, 0.f);
		out.rotationBias = vec3(0.f, 0.f, 0.f);

		if (dt > DT_THRESHOLD)
		{
			float a = dot(u, out.tangent);
			float b = dot(u, out.bitangent);
			out.translationBias = vec2(a, b) * (SLIDER_CONSTRAINT_BETA * invDt);

			quat rotationError = globalB.rotation * in.initialInvRotationDifference * conjugate(globalA.rotation);
			out.rotationBias = rotationError.v * (SLIDER_CONSTRAINT_BETA * invDt * 2.f);
		}

		out.globalSliderAxis = globalSliderAxis;
		float distanceAlongSlider = dot(u, globalSliderAxis);

		out.solveLimit = false;
		if (in.negDistanceLimit <= 0.f || in.posDistanceLimit >= 0.f)
		{
			bool minLimitViolated = (in.negDistanceLimit <= 0.f) && (distanceAlongSlider < in.negDistanceLimit);
			bool maxLimitViolated = (in.posDistanceLimit >= 0.f) && (distanceAlongSlider > in.posDistanceLimit);

			assert(!(minLimitViolated && maxLimitViolated));

			if (minLimitViolated || maxLimitViolated)
			{
				out.solveLimit = true;
				out.limitImpulse = 0.f;

				out.rAuxs = cross(rAu, globalSliderAxis);
				out.rBxs = cross(relGlobalAnchorB, globalSliderAxis);
				float invEffectiveAxialMass = invMassSum + dot(out.rAuxs, globalA.invInertia * out.rAuxs) + dot(out.rBxs, globalB.invInertia * out.rBxs);
				out.effectiveAxialMass = (invEffectiveAxialMass != 0.f) ? (1.f / invEffectiveAxialMass) : 0.f;
				out.limitSign = minLimitViolated ? 1.f : -1.f;

				out.limitBias = 0.f;
				if (dt > DT_THRESHOLD)
				{
					float error = minLimitViolated ? (distanceAlongSlider - in.negDistanceLimit) : (in.posDistanceLimit - distanceAlongSlider);
					out.limitBias = error * (SLIDER_LIMIT_CONSTRAINT_BETA * invDt);
				}

				out.limitImpulseToAngularVelocityA = globalA.invInertia * out.rAuxs;
				out.limitImpulseToAngularVelocityB = globalB.invInertia * out.rBxs;
			}
		}

		out.solveMotor = false;
		if (in.maxMotorForce > 0.f)
		{
			out.solveMotor = true;
			out.maxMotorImpulse = in.maxMotorForce * dt;
			out.motorImpulse = 0.f;

			out.motorVelocity = in.motorVelocity;
			if (in.motorType == constraint_position_motor)
			{
				// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
				// This will later get clamped to the maximum motor impulse.
				float minLimit = (in.negDistanceLimit <= 0.f) ? in.negDistanceLimit : -INFINITY;
				float maxLimit = (in.posDistanceLimit >= 0.f) ? in.posDistanceLimit : INFINITY;
				float targetDistance = clamp(in.motorTargetDistance, minLimit, maxLimit);
				out.motorVelocity = (dt > DT_THRESHOLD) ? ((targetDistance - distanceAlongSlider) * invDt) : 0.f;
			}
		}
	}

	slider_constraint_solver result;
	result.constraints = constraints;
	result.count = count;
	return result;
}

void solveSliderVelocityConstraints(slider_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve slider constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		slider_constraint_update& con = constraints.constraints[i];

		rigid_body_global_state& rbA = rbs[con.rigidBodyIndexA];
		rigid_body_global_state& rbB = rbs[con.rigidBodyIndexB];

		vec3 vA = rbA.linearVelocity;
		vec3 wA = rbA.angularVelocity;
		vec3 vB = rbB.linearVelocity;
		vec3 wB = rbB.angularVelocity;


		// Motor.
		if (con.solveMotor)
		{
			float Cdot = dot(vB, con.globalSliderAxis) - dot(vA, con.globalSliderAxis) - con.motorVelocity;
			float mass = 1.f / (rbA.invMass + rbB.invMass);

			float motorLambda = -mass * Cdot;
			float oldImpulse = con.motorImpulse;
			con.motorImpulse = clamp(con.motorImpulse + motorLambda, -con.maxMotorImpulse, con.maxMotorImpulse);
			motorLambda = con.motorImpulse - oldImpulse;

			vec3 P = motorLambda * con.globalSliderAxis;

			vA -= rbA.invMass * P;
			vB += rbB.invMass * P;
		}

		// Limit.
		if (con.solveLimit)
		{
			float Cdot = dot(vB, con.globalSliderAxis) + dot(wB, con.rBxs) - dot(vA, con.globalSliderAxis) - dot(wA, con.rAuxs);
			float limitLambda = -con.effectiveAxialMass * (con.limitSign * Cdot + con.limitBias);

			float impulse = max(con.limitImpulse + limitLambda, 0.f);
			limitLambda = impulse - con.limitImpulse;
			con.limitImpulse = impulse;

			limitLambda *= con.limitSign;

			vec3 P = limitLambda * con.globalSliderAxis;

			vA -= rbA.invMass * P;
			wA -= con.limitImpulseToAngularVelocityA * limitLambda;
			vB += rbB.invMass * P;
			wB += con.limitImpulseToAngularVelocityB * limitLambda;
		}
		
		// Rotation part.
		{
			vec3 Cdot = wB - wA;

			vec3 rotationLambda = solveLinearSystem(con.invEffectiveRotationMass, -(Cdot + con.rotationBias));
			wA -= rbA.invInertia * rotationLambda;
			wB += rbB.invInertia * rotationLambda;
		}

		// Position part.
		{
			vec2 Cdot;
			Cdot.x = dot(con.tangent, vB) + dot(con.rBxt, wB) - dot(con.tangent, vA) - dot(con.rAuxt, wA);
			Cdot.y = dot(con.bitangent, vB) + dot(con.rBxb, wB) - dot(con.bitangent, vA) - dot(con.rAuxb, wA);

			vec2 translationLambda = solveLinearSystem(con.invEffectiveTranslationMass, -(Cdot + con.translationBias));

			vec3 tb = con.tangent * translationLambda.x + con.bitangent * translationLambda.y;

			vA -= rbA.invMass * tb;
			wA -= rbA.invInertia * (con.rAuxt * translationLambda.x + con.rAuxb * translationLambda.y);
			vB += rbB.invMass * tb;
			wB += rbB.invInertia * (con.rBxt * translationLambda.x + con.rBxb * translationLambda.y);
		}


		rbA.linearVelocity = vA;
		rbA.angularVelocity = wA;
		rbB.linearVelocity = vB;
		rbB.angularVelocity = wB;
	}
}

simd_slider_constraint_solver initializeSliderVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const slider_constraint* input, const constraint_body_pair* bodyPairs, uint32 count, float dt)
{
	CPU_PROFILE_BLOCK("Initialize slider constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(count);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, count, UINT16_MAX, contactSlots);

	simd_slider_constraint_batch* batches = arena.allocate<simd_slider_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw invDt = 1.f / dt;
	const floatw one = 1.f;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_slider_constraint_batch& batch = batches[i];

		uint16 constraintIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			constraintIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}


		// Load body A.
		vec3w localCOGPositionA;
		quatw rotationA;
		vec3w positionA;
		mat3w invInertiaA;
		floatw invMassA;

		load8(&rbs->rotation.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			rotationA.x, rotationA.y, rotationA.z, rotationA.w,
			localCOGPositionA.x, localCOGPositionA.y, localCOGPositionA.z,
			positionA.x);

		load8(&rbs->position.y, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.y, positionA.z,
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21);

		load4(&rbs->invInertia.m02, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m02, invInertiaA.m12, invInertiaA.m22,
			invMassA);


		// Load body B.
		quatw rotationB;
		vec3w positionB;
		vec3w localCOGPositionB;
		mat3w invInertiaB;
		floatw invMassB;

		load8(&rbs->rotation.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			rotationB.x, rotationB.y, rotationB.z, rotationB.w,
			localCOGPositionB.x, localCOGPositionB.y, localCOGPositionB.z,
			positionB.x);

		load8(&rbs->position.y, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.y, positionB.z,
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21);

		load4(&rbs->invInertia.m02, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m02, invInertiaB.m12, invInertiaB.m22,
			invMassB);


		quatw initialInvRotationDifference;
		vec3w localAnchorA;
		vec3w localAnchorB;
		vec3w localAxisA;

		floatw negDistanceLimit;
		floatw posDistanceLimit;
		floatw maxMotorForce;

		load8((float*)input, constraintIndices, (uint32)sizeof(*input),
			initialInvRotationDifference.x, initialInvRotationDifference.y, initialInvRotationDifference.z, initialInvRotationDifference.w,
			localAnchorA.x, localAnchorA.y, localAnchorA.z,
			localAnchorB.x);

		load8(&input->localAnchorB.y, constraintIndices, (uint32)sizeof(*input),
			localAnchorB.y, localAnchorB.z,
			localAxisA.x, localAxisA.y, localAxisA.z,
			negDistanceLimit, posDistanceLimit, 
			maxMotorForce);



		// Relative to COG.
		vec3w relGlobalAnchorA = rotationA * (localAnchorA - localCOGPositionA);
		vec3w relGlobalAnchorB = rotationB * (localAnchorB - localCOGPositionB);

		// Global.
		vec3w globalAnchorA = positionA + relGlobalAnchorA;
		vec3w globalAnchorB = positionB + relGlobalAnchorB;

		vec3w globalSliderAxis = rotationA * localAxisA;

		vec3w tangent, bitangent;
		getTangents(globalSliderAxis, tangent, bitangent);
		vec3w u = globalAnchorB - globalAnchorA;

		vec3w rAu = relGlobalAnchorA + u;
		vec3w rBxt = cross(relGlobalAnchorB, tangent);
		vec3w rBxb = cross(relGlobalAnchorB, bitangent);
		vec3w rAuxt = cross(rAu, tangent);
		vec3w rAuxb = cross(rAu, bitangent);

		vec3w iArAuxt = invInertiaA * rAuxt;
		vec3w iArAuxb = invInertiaA * rAuxb;
		vec3w iBrBxt = invInertiaB * rBxt;
		vec3w iBrBxb = invInertiaB * rBxb;
		floatw invMassSum = invMassA + invMassB;

		mat2w invEffectiveTranslationMass;
		invEffectiveTranslationMass.m00 = dot(rAuxt, iArAuxt) + dot(rBxt, iBrBxt) + invMassSum;
		invEffectiveTranslationMass.m01 = dot(rAuxt, iArAuxb) + dot(rBxt, iBrBxb);
		invEffectiveTranslationMass.m10 = dot(rAuxb, iArAuxt) + dot(rBxb, iBrBxt);
		invEffectiveTranslationMass.m11 = dot(rAuxb, iArAuxb) + dot(rBxb, iBrBxb) + invMassSum;

		mat3w invEffectiveRotationMass = invInertiaA + invInertiaB;
		vec2w translationBias = zero;
		vec3w rotationBias = zero;

		if (dt > DT_THRESHOLD)
		{
			floatw a = dot(u, tangent);
			floatw b = dot(u, bitangent);
			translationBias = vec2w(a, b) * (SLIDER_CONSTRAINT_BETA * invDt);

			quatw rotationError = rotationB * initialInvRotationDifference * conjugate(rotationA);
			rotationBias = rotationError.v * (SLIDER_CONSTRAINT_BETA * 2.f * invDt);
		}

		rAuxt.x.store(batch.rAuxt[0]);
		rAuxt.y.store(batch.rAuxt[1]);
		rAuxt.z.store(batch.rAuxt[2]);

		rAuxb.x.store(batch.rAuxb[0]);
		rAuxb.y.store(batch.rAuxb[1]);
		rAuxb.z.store(batch.rAuxb[2]);

		rBxt.x.store(batch.rBxt[0]);
		rBxt.y.store(batch.rBxt[1]);
		rBxt.z.store(batch.rBxt[2]);

		rBxb.x.store(batch.rBxb[0]);
		rBxb.y.store(batch.rBxb[1]);
		rBxb.z.store(batch.rBxb[2]);

		tangent.x.store(batch.tangent[0]);
		tangent.y.store(batch.tangent[1]);
		tangent.z.store(batch.tangent[2]);

		bitangent.x.store(batch.bitangent[0]);
		bitangent.y.store(batch.bitangent[1]);
		bitangent.z.store(batch.bitangent[2]);

		invEffectiveTranslationMass.m[0].store(batch.invEffectiveTranslationMass[0]);
		invEffectiveTranslationMass.m[1].store(batch.invEffectiveTranslationMass[1]);
		invEffectiveTranslationMass.m[2].store(batch.invEffectiveTranslationMass[2]);
		invEffectiveTranslationMass.m[3].store(batch.invEffectiveTranslationMass[3]);

		translationBias.x.store(batch.translationBias[0]);
		translationBias.y.store(batch.translationBias[1]);

		invEffectiveRotationMass.m[0].store(batch.invEffectiveRotationMass[0]);
		invEffectiveRotationMass.m[1].store(batch.invEffectiveRotationMass[1]);
		invEffectiveRotationMass.m[2].store(batch.invEffectiveRotationMass[2]);
		invEffectiveRotationMass.m[3].store(batch.invEffectiveRotationMass[3]);
		invEffectiveRotationMass.m[4].store(batch.invEffectiveRotationMass[4]);
		invEffectiveRotationMass.m[5].store(batch.invEffectiveRotationMass[5]);
		invEffectiveRotationMass.m[6].store(batch.invEffectiveRotationMass[6]);
		invEffectiveRotationMass.m[7].store(batch.invEffectiveRotationMass[7]);
		invEffectiveRotationMass.m[8].store(batch.invEffectiveRotationMass[8]);

		rotationBias.x.store(batch.rotationBias[0]);
		rotationBias.y.store(batch.rotationBias[1]);
		rotationBias.z.store(batch.rotationBias[2]);

		globalSliderAxis.x.store(batch.globalSliderAxis[0]);
		globalSliderAxis.y.store(batch.globalSliderAxis[1]);
		globalSliderAxis.z.store(batch.globalSliderAxis[2]);


		floatw distanceAlongSlider = dot(u, globalSliderAxis);


		auto minLimitActive = negDistanceLimit <= zero;
		auto maxLimitActive = posDistanceLimit >= zero;

		if (anyTrue(minLimitActive | maxLimitActive))
		{
			auto minLimitViolated = minLimitActive & (distanceAlongSlider < negDistanceLimit);
			auto maxLimitViolated = maxLimitActive & (distanceAlongSlider > posDistanceLimit);

			auto limitViolated = minLimitViolated | maxLimitViolated;
			batch.solveLimit = anyTrue(limitViolated);

			if (batch.solveLimit)
			{
				vec3w rAuxs = cross(rAu, globalSliderAxis);
				vec3w rBxs = cross(relGlobalAnchorB, globalSliderAxis);
				floatw invEffectiveAxialMass = invMassSum + dot(rAuxs, invInertiaA * rAuxs) + dot(rBxs, invInertiaB * rBxs);
				floatw effectiveAxialMass = ifThen(invEffectiveAxialMass != zero, one / invEffectiveAxialMass, zero);
				effectiveAxialMass = ifThen(limitViolated, effectiveAxialMass, zero);
				floatw limitSign = ifThen(minLimitViolated, one, -one);

				floatw limitBias = zero;
				if (dt > DT_THRESHOLD)
				{
					floatw error = ifThen(minLimitViolated, distanceAlongSlider - negDistanceLimit, posDistanceLimit - distanceAlongSlider);
					limitBias = error * (SLIDER_LIMIT_CONSTRAINT_BETA * invDt);
				}

				vec3w limitImpulseToAngularVelocityA = invInertiaA * rAuxs;
				vec3w limitImpulseToAngularVelocityB = invInertiaB * rBxs;

				rAuxs.x.store(batch.rAuxs[0]);
				rAuxs.y.store(batch.rAuxs[1]);
				rAuxs.z.store(batch.rAuxs[2]);

				rBxs.x.store(batch.rBxs[0]);
				rBxs.y.store(batch.rBxs[1]);
				rBxs.z.store(batch.rBxs[2]);

				zero.store(batch.limitImpulse);
				effectiveAxialMass.store(batch.effectiveAxialMass);
				limitSign.store(batch.limitSign);
				limitBias.store(batch.limitBias);

				limitImpulseToAngularVelocityA.x.store(batch.limitImpulseToAngularVelocityA[0]);
				limitImpulseToAngularVelocityA.y.store(batch.limitImpulseToAngularVelocityA[1]);
				limitImpulseToAngularVelocityA.z.store(batch.limitImpulseToAngularVelocityA[2]);

				limitImpulseToAngularVelocityB.x.store(batch.limitImpulseToAngularVelocityB[0]);
				limitImpulseToAngularVelocityB.y.store(batch.limitImpulseToAngularVelocityB[1]);
				limitImpulseToAngularVelocityB.z.store(batch.limitImpulseToAngularVelocityB[2]);
			}

		}




		auto motorActive = maxMotorForce > zero;
		batch.solveMotor = anyTrue(motorActive);
		if (batch.solveMotor)
		{
			floatw motorTypeF;
			floatw motorVelocity;
			floatw dummy0, dummy1;

			load4((float*)&input->motorType, constraintIndices, (uint32)sizeof(*input),
				motorTypeF, motorVelocity,
				dummy0, dummy1);

			intw motorType = reinterpret(motorTypeF);

			floatw maxMotorImpulse = maxMotorForce * dt;
			maxMotorImpulse = ifThen(motorActive, maxMotorImpulse, zero);

			auto isVelocityMotor = motorType == constraint_velocity_motor;

			if (anyFalse(isVelocityMotor))
			{
				// Inspired by Bullet Engine. We set the velocity such that the target angle is reached within one frame.
				// This will later get clamped to the maximum motor impulse.

				floatw motorTargetDistance = motorVelocity; // This is a union.

				floatw minLimit = ifThen(negDistanceLimit <= zero, negDistanceLimit, -INFINITY);
				floatw maxLimit = ifThen(posDistanceLimit >= zero, posDistanceLimit, INFINITY);
				floatw targetDistance = clamp(motorTargetDistance, minLimit, maxLimit);
				floatw motorVelocityOverride = (dt > DT_THRESHOLD) ? ((targetDistance - distanceAlongSlider) * invDt) : 0.f;
				motorVelocity = ifThen(reinterpret(isVelocityMotor), motorVelocity, motorVelocityOverride);
			}

			floatw effectiveMotorMass = one / invMassSum;
			effectiveMotorMass = ifThen(motorActive, effectiveMotorMass, zero);

			zero.store(batch.motorImpulse);
			maxMotorImpulse.store(batch.maxMotorImpulse);
			motorVelocity.store(batch.motorVelocity);
			effectiveMotorMass.store(batch.effectiveMotorMass);
		}

	}

	simd_slider_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveSliderVelocityConstraintsSIMD(simd_slider_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve slider constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_slider_constraint_batch& batch = constraints.batches[i];


		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		mat3w invInertiaA;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20,
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21,
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		mat3w invInertiaB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		vec3w globalSliderAxis(batch.globalSliderAxis[0], batch.globalSliderAxis[1], batch.globalSliderAxis[2]);

		// Motor.
		if (batch.solveMotor)
		{
			floatw motorVelocity(batch.motorVelocity);
			floatw motorImpulse(batch.motorImpulse);
			floatw maxMotorImpulse(batch.maxMotorImpulse);
			floatw effectiveMotorMass(batch.effectiveMotorMass);

			floatw Cdot = dot(vB, globalSliderAxis) - dot(vA, globalSliderAxis) - motorVelocity;

			floatw motorLambda = -effectiveMotorMass * Cdot;
			floatw oldImpulse = motorImpulse;
			motorImpulse = clamp(motorImpulse + motorLambda, -maxMotorImpulse, maxMotorImpulse);
			motorLambda = motorImpulse - oldImpulse;

			vec3w P = motorLambda * globalSliderAxis;

			vA -= invMassA * P;
			vB += invMassB * P;

			motorImpulse.store(batch.motorImpulse);
		}

		// Limit.
		if (batch.solveLimit)
		{
			floatw limitSign(batch.limitSign);
			floatw limitBias(batch.limitBias);
			floatw effectiveAxialMass(batch.effectiveAxialMass);
			floatw limitImpulse(batch.limitImpulse);

			vec3w rAuxs(batch.rAuxs[0], batch.rAuxs[1], batch.rAuxs[2]);
			vec3w rBxs(batch.rBxs[0], batch.rBxs[1], batch.rBxs[2]);

			vec3w limitImpulseToAngularVelocityA(batch.limitImpulseToAngularVelocityA[0], batch.limitImpulseToAngularVelocityA[1], batch.limitImpulseToAngularVelocityA[2]);
			vec3w limitImpulseToAngularVelocityB(batch.limitImpulseToAngularVelocityB[0], batch.limitImpulseToAngularVelocityB[1], batch.limitImpulseToAngularVelocityB[2]);

			floatw Cdot = dot(vB, globalSliderAxis) + dot(wB, rBxs) - dot(vA, globalSliderAxis) - dot(wA, rAuxs);
			floatw limitLambda = -effectiveAxialMass * (limitSign * Cdot + limitBias);

			floatw impulse = maximum(limitImpulse + limitLambda, 0.f);
			limitLambda = impulse - limitImpulse;
			limitImpulse = impulse;

			limitLambda *= limitSign;

			vec3w P = limitLambda * globalSliderAxis;

			vA -= invMassA * P;
			wA -= limitImpulseToAngularVelocityA * limitLambda;
			vB += invMassB * P;
			wB += limitImpulseToAngularVelocityB * limitLambda;

			limitImpulse.store(batch.limitImpulse);
		}


		// Rotation part.
		{
			mat3w invEffectiveRotationMass;
			invEffectiveRotationMass.m[0] = floatw(batch.invEffectiveRotationMass[0]);
			invEffectiveRotationMass.m[1] = floatw(batch.invEffectiveRotationMass[1]);
			invEffectiveRotationMass.m[2] = floatw(batch.invEffectiveRotationMass[2]);
			invEffectiveRotationMass.m[3] = floatw(batch.invEffectiveRotationMass[3]);
			invEffectiveRotationMass.m[4] = floatw(batch.invEffectiveRotationMass[4]);
			invEffectiveRotationMass.m[5] = floatw(batch.invEffectiveRotationMass[5]);
			invEffectiveRotationMass.m[6] = floatw(batch.invEffectiveRotationMass[6]);
			invEffectiveRotationMass.m[7] = floatw(batch.invEffectiveRotationMass[7]);
			invEffectiveRotationMass.m[8] = floatw(batch.invEffectiveRotationMass[8]);

			vec3w rotationBias(batch.rotationBias[0], batch.rotationBias[1], batch.rotationBias[2]);

			vec3w Cdot = wB - wA;

			vec3w rotationLambda = solveLinearSystem(invEffectiveRotationMass, -(Cdot + rotationBias));
			wA -= invInertiaA * rotationLambda;
			wB += invInertiaB * rotationLambda;
		}

		// Position part.
		{
			vec3w tangent(batch.tangent[0], batch.tangent[1], batch.tangent[2]);
			vec3w bitangent(batch.bitangent[0], batch.bitangent[1], batch.bitangent[2]);
			vec3w rAuxt(batch.rAuxt[0], batch.rAuxt[1], batch.rAuxt[2]);
			vec3w rAuxb(batch.rAuxb[0], batch.rAuxb[1], batch.rAuxb[2]);
			vec3w rBxt(batch.rBxt[0], batch.rBxt[1], batch.rBxt[2]);
			vec3w rBxb(batch.rBxb[0], batch.rBxb[1], batch.rBxb[2]);

			mat2w invEffectiveTranslationMass;
			invEffectiveTranslationMass.m[0] = floatw(batch.invEffectiveTranslationMass[0]);
			invEffectiveTranslationMass.m[1] = floatw(batch.invEffectiveTranslationMass[1]);
			invEffectiveTranslationMass.m[2] = floatw(batch.invEffectiveTranslationMass[2]);
			invEffectiveTranslationMass.m[3] = floatw(batch.invEffectiveTranslationMass[3]);

			vec2w translationBias(batch.translationBias[0], batch.translationBias[1]);

			vec2w Cdot;
			Cdot.x = dot(tangent, vB) + dot(rBxt, wB) - dot(tangent, vA) - dot(rAuxt, wA);
			Cdot.y = dot(bitangent, vB) + dot(rBxb, wB) - dot(bitangent, vA) - dot(rAuxb, wA);

			vec2w translationLambda = solveLinearSystem(invEffectiveTranslationMass, -(Cdot + translationBias));

			vec3w tb = tangent * translationLambda.x + bitangent * translationLambda.y;

			vA -= invMassA * tb;
			wA -= invInertiaA * (rAuxt * translationLambda.x + rAuxb * translationLambda.y);
			vB += invMassB * tb;
			wB += invInertiaB * (rBxt * translationLambda.x + rBxb * translationLambda.y);
		}


		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}


collision_constraint_solver initializeCollisionVelocityConstraints(memory_arena& arena, const rigid_body_global_state* rbs, const collision_contact* contacts, const constraint_body_pair* bodyPairs, uint32 numContacts, float dt)
{
	CPU_PROFILE_BLOCK("Initialize collision constraints");

	float invDt = 1.f / dt;

	collision_constraint* constraints = arena.allocate<collision_constraint>(numContacts);

	for (uint32 contactID = 0; contactID < numContacts; ++contactID)
	{
		collision_constraint& constraint = constraints[contactID];
		const collision_contact& contact = contacts[contactID];
		constraint_body_pair pair = bodyPairs[contactID];

		auto& rbA = rbs[pair.rbA];
		auto& rbB = rbs[pair.rbB];

		constraint.impulseInNormalDir = 0.f;
		constraint.impulseInTangentDir = 0.f;

		constraint.relGlobalAnchorA = contact.point - rbA.position;
		constraint.relGlobalAnchorB = contact.point - rbB.position;

		vec3 anchorVelocityA = rbA.linearVelocity + cross(rbA.angularVelocity, constraint.relGlobalAnchorA);
		vec3 anchorVelocityB = rbB.linearVelocity + cross(rbB.angularVelocity, constraint.relGlobalAnchorB);

		vec3 relVelocity = anchorVelocityB - anchorVelocityA;
		constraint.tangent = relVelocity - dot(contact.normal, relVelocity) * contact.normal;
		constraint.tangent = noz(constraint.tangent);

		{ // Tangent direction.
			vec3 crAt = cross(constraint.relGlobalAnchorA, constraint.tangent);
			vec3 crBt = cross(constraint.relGlobalAnchorB, constraint.tangent);
			float invMassInTangentDir = rbA.invMass + dot(crAt, rbA.invInertia * crAt)
									  + rbB.invMass + dot(crBt, rbB.invInertia * crBt);
			constraint.effectiveMassInTangentDir = (invMassInTangentDir != 0.f) ? (1.f / invMassInTangentDir) : 0.f;

			constraint.tangentImpulseToAngularVelocityA = rbA.invInertia * crAt;
			constraint.tangentImpulseToAngularVelocityB = rbB.invInertia * crBt;
		}

		{ // Normal direction.
			vec3 crAn = cross(constraint.relGlobalAnchorA, contact.normal);
			vec3 crBn = cross(constraint.relGlobalAnchorB, contact.normal);
			float invMassInNormalDir = rbA.invMass + dot(crAn, rbA.invInertia * crAn)
									 + rbB.invMass + dot(crBn, rbB.invInertia * crBn);
			constraint.effectiveMassInNormalDir = (invMassInNormalDir != 0.f) ? (1.f / invMassInNormalDir) : 0.f;

			constraint.bias = 0.f;

			if (dt > DT_THRESHOLD)
			{
				float vRel = dot(contact.normal, relVelocity);
				const float slop = -0.001f;
				if (-contact.penetrationDepth < slop && vRel < 0.f)
				{
					float restitution = (float)(contact.friction_restitution & 0xFFFF) / (float)0xFFFF;
					constraint.bias = -restitution * vRel - 0.1f * (-contact.penetrationDepth - slop) * invDt;
				}
			}

			constraint.normalImpulseToAngularVelocityA = rbA.invInertia * crAn;
			constraint.normalImpulseToAngularVelocityB = rbB.invInertia * crBn;
		}
	}

	collision_constraint_solver result;
	result.constraints = constraints;
	result.bodyPairs = bodyPairs;
	result.contacts = contacts;
	result.count = numContacts;
	return result;
}

void solveCollisionVelocityConstraints(collision_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve collision constraints");

	for (uint32 i = 0; i < constraints.count; ++i)
	{
		const collision_contact& contact = constraints.contacts[i];
		collision_constraint& constraint = constraints.constraints[i];
		constraint_body_pair pair = constraints.bodyPairs[i];

		auto& rbA = rbs[pair.rbA];
		auto& rbB = rbs[pair.rbB];

		if (rbA.invMass == 0.f && rbB.invMass == 0.f)
		{
			continue;
		}

		vec3 vA = rbA.linearVelocity;
		vec3 wA = rbA.angularVelocity;
		vec3 vB = rbB.linearVelocity;
		vec3 wB = rbB.angularVelocity;

		{ // Tangent dir.
			vec3 anchorVelocityA = vA + cross(wA, constraint.relGlobalAnchorA);
			vec3 anchorVelocityB = vB + cross(wB, constraint.relGlobalAnchorB);

			vec3 relVelocity = anchorVelocityB - anchorVelocityA;
			float vt = dot(relVelocity, constraint.tangent);
			float lambda = -constraint.effectiveMassInTangentDir * vt;

			float friction = (float)(contact.friction_restitution >> 16) / (float)0xFFFF;
			float maxFriction = friction * constraint.impulseInNormalDir;
			assert(maxFriction >= 0.f);
			float newImpulse = clamp(constraint.impulseInTangentDir + lambda, -maxFriction, maxFriction);
			lambda = newImpulse - constraint.impulseInTangentDir;
			constraint.impulseInTangentDir = newImpulse;

			vec3 P = lambda * constraint.tangent;
			vA -= rbA.invMass * P;
			wA -= constraint.tangentImpulseToAngularVelocityA * lambda;
			vB += rbB.invMass * P;
			wB += constraint.tangentImpulseToAngularVelocityB * lambda;
		}

		{ // Normal dir.
			vec3 anchorVelocityA = vA + cross(wA, constraint.relGlobalAnchorA);
			vec3 anchorVelocityB = vB + cross(wB, constraint.relGlobalAnchorB);

			vec3 relVelocity = anchorVelocityB - anchorVelocityA;
			float vn = dot(relVelocity, contact.normal);
			float lambda = -constraint.effectiveMassInNormalDir * (vn - constraint.bias);
			float impulse = max(constraint.impulseInNormalDir + lambda, 0.f);
			lambda = impulse - constraint.impulseInNormalDir;
			constraint.impulseInNormalDir = impulse;

			vec3 P = lambda * contact.normal;
			vA -= rbA.invMass * P;
			wA -= constraint.normalImpulseToAngularVelocityA * lambda;
			vB += rbB.invMass * P;
			wB += constraint.normalImpulseToAngularVelocityB * lambda;
		}

		rbA.linearVelocity = vA;
		rbA.angularVelocity = wA;
		rbB.linearVelocity = vB;
		rbB.angularVelocity = wB;
	}
}

simd_collision_constraint_solver initializeCollisionVelocityConstraintsSIMD(memory_arena& arena, const rigid_body_global_state* rbs, const collision_contact* contacts, const constraint_body_pair* bodyPairs, uint32 numContacts, uint16 dummyRigidBodyIndex, float dt)
{
	CPU_PROFILE_BLOCK("Initialize collision constraints SIMD");

	simd_constraint_slot* contactSlots = arena.allocate<simd_constraint_slot>(numContacts);
	uint32 numBatches = scheduleConstraintsSIMD(arena, bodyPairs, numContacts, dummyRigidBodyIndex, contactSlots);

	simd_collision_constraint_batch* batches = arena.allocate<simd_collision_constraint_batch>(numBatches);

	const floatw zero = floatw::zero();
	const floatw slop = -0.001f;
	const floatw scale = 0.1f;
	const floatw invDt = 1.f / dt;

	for (uint32 i = 0; i < numBatches; ++i)
	{
		const simd_constraint_slot& slot = contactSlots[i];
		simd_collision_constraint_batch& batch = batches[i];

		uint16 contactIndices[CONSTRAINT_SIMD_WIDTH];
		for (uint32 j = 0; j < CONSTRAINT_SIMD_WIDTH; ++j)
		{
			contactIndices[j] = (uint16)slot.indices[j];
			batch.rbAIndices[j] = bodyPairs[slot.indices[j]].rbA;
			batch.rbBIndices[j] = bodyPairs[slot.indices[j]].rbB;
		}

		vec3w point, normal;
		floatw penetrationDepth, friction_restitutionF;
		load8((float*)contacts, contactIndices, (uint32)sizeof(collision_contact),
			point.x, point.y, point.z, penetrationDepth, normal.x, normal.y, normal.z, friction_restitutionF);
		intw friction_restitution = reinterpret(friction_restitutionF);

		floatw friction = convert(friction_restitution >> 16) / floatw(0xFFFF);
		floatw restitution = convert(friction_restitution & 0xFFFF) / floatw(0xFFFF);


		// Load body A.
		vec3w vA, wA;
		mat3w invInertiaA;
		floatw invMassA;
		vec3w positionA;
		floatw unused;

		load8(&rbs->invInertia.m00, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m00, invInertiaA.m10, invInertiaA.m20, 
			invInertiaA.m01, invInertiaA.m11, invInertiaA.m21, 
			invInertiaA.m02, invInertiaA.m12);

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaA.m22, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		load4(&rbs->position.x, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			positionA.x, positionA.y, positionA.z, unused);


		// Load body B.
		vec3w vB, wB;
		mat3w invInertiaB;
		floatw invMassB;
		vec3w positionB;

		load8(&rbs->invInertia.m00, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m00, invInertiaB.m10, invInertiaB.m20,
			invInertiaB.m01, invInertiaB.m11, invInertiaB.m21,
			invInertiaB.m02, invInertiaB.m12);

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			invInertiaB.m22, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);

		load4(&rbs->position.x, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			positionB.x, positionB.y, positionB.z, unused);





		vec3w relGlobalAnchorA = point - positionA;
		vec3w relGlobalAnchorB = point - positionB;

		vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
		vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);

		vec3w relVelocity = anchorVelocityB - anchorVelocityA;
		vec3w tangent = relVelocity - dot(normal, relVelocity) * normal;
		tangent = noz(tangent);

		relGlobalAnchorA.x.store(batch.relGlobalAnchorA[0]);
		relGlobalAnchorA.y.store(batch.relGlobalAnchorA[1]);
		relGlobalAnchorA.z.store(batch.relGlobalAnchorA[2]);

		relGlobalAnchorB.x.store(batch.relGlobalAnchorB[0]);
		relGlobalAnchorB.y.store(batch.relGlobalAnchorB[1]);
		relGlobalAnchorB.z.store(batch.relGlobalAnchorB[2]);

		normal.x.store(batch.normal[0]);
		normal.y.store(batch.normal[1]);
		normal.z.store(batch.normal[2]);

		tangent.x.store(batch.tangent[0]);
		tangent.y.store(batch.tangent[1]);
		tangent.z.store(batch.tangent[2]);

		zero.store(batch.impulseInNormalDir);
		zero.store(batch.impulseInTangentDir);
		friction.store(batch.friction);



		{ // Tangent direction.
			vec3w crAt = cross(relGlobalAnchorA, tangent);
			vec3w crBt = cross(relGlobalAnchorB, tangent);
			floatw invMassInTangentDir = invMassA + dot(crAt, invInertiaA * crAt)
				+ invMassB + dot(crBt, invInertiaB * crBt);
			floatw effectiveMassInTangentDir = ifThen(invMassInTangentDir != zero, 1.f / invMassInTangentDir, zero);
			effectiveMassInTangentDir.store(batch.effectiveMassInTangentDir);

			vec3w tangentImpulseToAngularVelocityA = invInertiaA * crAt;
			tangentImpulseToAngularVelocityA.x.store(batch.tangentImpulseToAngularVelocityA[0]);
			tangentImpulseToAngularVelocityA.y.store(batch.tangentImpulseToAngularVelocityA[1]);
			tangentImpulseToAngularVelocityA.z.store(batch.tangentImpulseToAngularVelocityA[2]);

			vec3w tangentImpulseToAngularVelocityB = invInertiaB * crBt;
			tangentImpulseToAngularVelocityB.x.store(batch.tangentImpulseToAngularVelocityB[0]);
			tangentImpulseToAngularVelocityB.y.store(batch.tangentImpulseToAngularVelocityB[1]);
			tangentImpulseToAngularVelocityB.z.store(batch.tangentImpulseToAngularVelocityB[2]);
		}


		{ // Normal direction.
			vec3w crAn = cross(relGlobalAnchorA, normal);
			vec3w crBn = cross(relGlobalAnchorB, normal);
			floatw invMassInNormalDir = invMassA + dot(crAn, invInertiaA * crAn)
				+ invMassB + dot(crBn, invInertiaB * crBn);
			floatw effectiveMassInNormalDir = ifThen(invMassInNormalDir != zero, 1.f / invMassInNormalDir, zero);

			floatw bias = zero;

			if (dt > DT_THRESHOLD)
			{
				floatw vRel = dot(normal, relVelocity);

				floatw bounceBias = -restitution * vRel - scale * (-penetrationDepth - slop) * invDt;
				bias = ifThen((-penetrationDepth < slop) & (vRel < zero), bounceBias, bias);
			}

			effectiveMassInNormalDir.store(batch.effectiveMassInNormalDir);
			bias.store(batch.bias);

			vec3w normalImpulseToAngularVelocityA = invInertiaA * crAn;
			normalImpulseToAngularVelocityA.x.store(batch.normalImpulseToAngularVelocityA[0]);
			normalImpulseToAngularVelocityA.y.store(batch.normalImpulseToAngularVelocityA[1]);
			normalImpulseToAngularVelocityA.z.store(batch.normalImpulseToAngularVelocityA[2]);

			vec3w normalImpulseToAngularVelocityB = invInertiaB * crBn;
			normalImpulseToAngularVelocityB.x.store(batch.normalImpulseToAngularVelocityB[0]);
			normalImpulseToAngularVelocityB.y.store(batch.normalImpulseToAngularVelocityB[1]);
			normalImpulseToAngularVelocityB.z.store(batch.normalImpulseToAngularVelocityB[2]);
		}
	}

	simd_collision_constraint_solver result;
	result.batches = batches;
	result.numBatches = numBatches;
	return result;
}

void solveCollisionVelocityConstraintsSIMD(simd_collision_constraint_solver constraints, rigid_body_global_state* rbs)
{
	CPU_PROFILE_BLOCK("Solve collision constraints SIMD");

	for (uint32 i = 0; i < constraints.numBatches; ++i)
	{
		simd_collision_constraint_batch& batch = constraints.batches[i];

		// Load body A.
		vec3w vA, wA;
		floatw invMassA;
		floatw dummyA;

		load8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			dummyA, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);


		// Load body B.
		vec3w vB, wB;
		floatw invMassB;
		floatw dummyB;

		load8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			dummyB, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);


		// Load constraint.
		vec3w relGlobalAnchorA(batch.relGlobalAnchorA[0], batch.relGlobalAnchorA[1], batch.relGlobalAnchorA[2]);
		vec3w relGlobalAnchorB(batch.relGlobalAnchorB[0], batch.relGlobalAnchorB[1], batch.relGlobalAnchorB[2]);
		vec3w normal(batch.normal[0], batch.normal[1], batch.normal[2]);
		vec3w tangent(batch.tangent[0], batch.tangent[1], batch.tangent[2]);
		floatw effectiveMassInNormalDir(batch.effectiveMassInNormalDir);
		floatw effectiveMassInTangentDir(batch.effectiveMassInTangentDir);
		floatw friction(batch.friction);
		floatw impulseInNormalDir(batch.impulseInNormalDir);
		floatw impulseInTangentDir(batch.impulseInTangentDir);
		floatw bias(batch.bias);

		vec3w tangentImpulseToAngularVelocityA(batch.tangentImpulseToAngularVelocityA[0], batch.tangentImpulseToAngularVelocityA[1], batch.tangentImpulseToAngularVelocityA[2]);
		vec3w tangentImpulseToAngularVelocityB(batch.tangentImpulseToAngularVelocityB[0], batch.tangentImpulseToAngularVelocityB[1], batch.tangentImpulseToAngularVelocityB[2]);

		{ // Tangent dir.
			vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
			vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);

			vec3w relVelocity = anchorVelocityB - anchorVelocityA;
			floatw vt = dot(relVelocity, tangent);
			floatw lambda = -effectiveMassInTangentDir * vt;

			floatw maxFriction = friction * impulseInNormalDir;
			floatw newImpulse = clamp(impulseInTangentDir + lambda, -maxFriction, maxFriction);
			lambda = newImpulse - impulseInTangentDir;
			impulseInTangentDir = newImpulse;

			vec3w P = lambda * tangent;
			vA -= invMassA * P;
			wA -= tangentImpulseToAngularVelocityA * lambda;
			vB += invMassB * P;
			wB += tangentImpulseToAngularVelocityB * lambda;
		}

		vec3w normalImpulseToAngularVelocityA(batch.normalImpulseToAngularVelocityA[0], batch.normalImpulseToAngularVelocityA[1], batch.normalImpulseToAngularVelocityA[2]);
		vec3w normalImpulseToAngularVelocityB(batch.normalImpulseToAngularVelocityB[0], batch.normalImpulseToAngularVelocityB[1], batch.normalImpulseToAngularVelocityB[2]);

		{ // Normal dir.
			vec3w anchorVelocityA = vA + cross(wA, relGlobalAnchorA);
			vec3w anchorVelocityB = vB + cross(wB, relGlobalAnchorB);

			vec3w relVelocity = anchorVelocityB - anchorVelocityA;
			floatw vn = dot(relVelocity, normal);
			floatw lambda = -effectiveMassInNormalDir * (vn - bias);
			floatw impulse = maximum(impulseInNormalDir + lambda, floatw::zero());
			lambda = impulse - impulseInNormalDir;
			impulseInNormalDir = impulse;

			vec3w P = lambda * normal;
			vA -= invMassA * P;
			wA -= normalImpulseToAngularVelocityA * lambda;
			vB += invMassB * P;
			wB += normalImpulseToAngularVelocityB * lambda;
		}

		impulseInNormalDir.store(batch.impulseInNormalDir);
		impulseInTangentDir.store(batch.impulseInTangentDir);

		store8(&rbs->invInertia.m22, batch.rbAIndices, (uint32)sizeof(rigid_body_global_state),
			dummyA, invMassA, vA.x, vA.y, vA.z, wA.x, wA.y, wA.z);

		store8(&rbs->invInertia.m22, batch.rbBIndices, (uint32)sizeof(rigid_body_global_state),
			dummyB, invMassB, vB.x, vB.y, vB.z, wB.x, wB.y, wB.z);
	}
}


