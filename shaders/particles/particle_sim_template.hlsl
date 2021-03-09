#include "cs.hlsli"
#include "particles_rs.hlsli"

RWStructuredBuffer<particle_draw> drawInfo				: register(u1);
RWStructuredBuffer<particle_counters> counters			: register(u2);

RWStructuredBuffer<particle_data> particles				: register(u3);
RWStructuredBuffer<uint> deadList						: register(u4);
RWStructuredBuffer<uint> currentAliveList				: register(u5);
RWStructuredBuffer<uint> newAliveList					: register(u6);


[numthreads(PARTICLES_SIMULATE_BLOCK_SIZE, 1, 1)]
[RootSignature(PARTICLES_COMPUTE_RS)]
void main(cs_input IN)
{
	const uint i = IN.dispatchThreadID.x;
	const uint count = counters[0].numAliveParticlesThisFrame;
	if (i >= count)
	{
		return;
	}

	float dt = 1.f / 60.f;

	uint index = currentAliveList[i];

	particle_data particle = particles[index];
	particle.life -= dt;
	if (particle.life > 0)
	{
		simulateParticle(particle, dt);
		particles[index] = particle;

		uint alive;
		InterlockedAdd(drawInfo[0].arguments.InstanceCount, 1, alive);
		newAliveList[alive] = index;
	}
	else
	{
		uint dead;
		InterlockedAdd(counters[0].numDeadParticles, 1, dead);
		deadList[dead] = index;
	}
}