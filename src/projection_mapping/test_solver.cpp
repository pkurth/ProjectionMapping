#include "pch.h"
#include "core/math.h"


struct projector_data
{
	float attenuation;
	float E;
	float solverIntensity;
	float partialSum;
};

void testProjectorSolver()
{
	projector_data projectors[16];
	uint32 numProjectors = 0;

	float targetIntensity = 0.f;

	projectors[numProjectors++] = { 0.2f };
	projectors[numProjectors++] = { 0.1f };
	projectors[numProjectors++] = { 0.3f };
	projectors[numProjectors++] = { 0.3f };

	uint32 totalNumProjectors = numProjectors; // For simulation below.


	targetIntensity = max(targetIntensity, 0.0001f);

	float ESum = 0.f;

	for (uint32 projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		projector_data& proj = projectors[projIndex];
		proj.E = pow(proj.attenuation, 4.f);
		ESum += proj.E;
	}

	uint32 myProjectorIndex = 1; // Keep track of the one projector we are interested in.


	float remainingIntensity = targetIntensity;
	float totalResultingIntensity = 0.f;

	const uint32 numIterations = 3;

	uint32 iteration = 0;
	for (; iteration < numIterations && numProjectors > 0; ++iteration)
	{
		float nextESum = ESum;
		float resultingIntensity = 0.f;

		for (uint32 projIndex = 0; projIndex < numProjectors; ++projIndex)
		{
			projector_data& proj = projectors[projIndex];

			float maxCompensation = (1.f - proj.partialSum) / remainingIntensity;

			float w = proj.E / ESum;
			float g = clamp(w / proj.attenuation, 0.f, maxCompensation);

			resultingIntensity += proj.attenuation * g * remainingIntensity;

			proj.partialSum += g * remainingIntensity;

			if (g == maxCompensation)
			{
				// Projector intensity is depleted. Remove from list for next iteration.
				assert(fuzzyEquals(proj.partialSum, 1.f));
				
				nextESum -= proj.E;



				if (myProjectorIndex == projIndex)
				{
					myProjectorIndex = numProjectors - 1;
				}
				else if (myProjectorIndex == numProjectors - 1)
				{
					myProjectorIndex = projIndex;
				}


				std::swap(projectors[projIndex], projectors[numProjectors - 1]); // Swap (don't overwrite), so that we can recover our projector of interest (and for the simulation below).
				--numProjectors;
				--projIndex;
			}
		}

		totalResultingIntensity += resultingIntensity;

		std::cout << "Total intensity after iteration " << iteration << ": " << totalResultingIntensity << ". " << numProjectors << " projectors remain to distribute remaining intensity.\n";

		if (resultingIntensity >= remainingIntensity - 0.001f)
		{
			break;
		}

		remainingIntensity -= resultingIntensity;

		ESum = nextESum;



		int a = 0;
	}


	std::cout << "Iterations: " << iteration << '\n';




	float resultingIntensity = 0.f;
	
	for (uint32 projIndex = 0; projIndex < totalNumProjectors; ++projIndex)
	{
		projector_data& proj = projectors[projIndex];

		proj.solverIntensity = proj.partialSum / targetIntensity;

		float maxCompensation = 1.f / targetIntensity;
		assert(proj.solverIntensity <= maxCompensation);
		resultingIntensity += proj.attenuation * proj.solverIntensity * targetIntensity;

		//resultingIntensity += proj.attenuation * proj.partialSum;
	}
	
	assert(resultingIntensity < targetIntensity + 0.001f);
	
	int a = 0;
}
