#include "pch.h"
#include "graycode.h"


static int32 binaryToGray(int32 num)
{
	return (num >> 1) ^ num;
}

static int32 grayToBinary(int32 value, int32 offset)
{
	for (int32 shift = 1; shift < 32; shift <<= 1)
	{
		value ^= value >> shift;
	}
	return value - offset;
}

uint32 getNumberOfGraycodePatternsRequired(uint32 width, uint32 height)
{
	uint32 vbits = 1;
	uint32 hbits = 1;
	for (uint32 i = (1 << vbits); i < width; i = (1 << vbits)) { vbits++; }
	for (uint32 i = (1 << hbits); i < height; i = (1 << hbits)) { hbits++; }
	return 2 * vbits + 2 * hbits + 2;
}

static void makePattern(uint8* pattern, uint32 numChannels, uint32 width, uint32 height, int vmask, int voffset, int hmask, int hoffset, int inverted, uint8 whiteValue)
{
	int tvalue = (inverted ? 0 : whiteValue);
	int fvalue = (inverted ? whiteValue : 0);

	for (uint32 h = 0; h < height; ++h)
	{
		uint8* row = pattern + h * width * numChannels;

		for (uint32 w = 0; w < width; ++w)
		{
			uint8* px = row + (numChannels * w);
			int test = (binaryToGray(h + hoffset) & hmask) + (binaryToGray(w + voffset) & vmask);
			int value = (test ? tvalue : fvalue);

			for (uint32 i = 0; i < numChannels; ++i)
			{
				px[i] = value;
			}
		}
	}
}

bool generateGraycodePattern(uint8* image, uint32 width, uint32 height, uint32 patternID, uint8 whiteValue)
{
	uint32 numChannels = 1;

	uint32 vbits = 1;
	uint32 hbits = 1;
	for (uint32 i = (1 << vbits); i < width; i = (1 << vbits)) { vbits++; }
	for (uint32 i = (1 << hbits); i < height; i = (1 << hbits)) { hbits++; }


	int vmask = 0, voffset = ((1 << vbits) - width) / 2, hmask = 0, hoffset = ((1 << hbits) - height) / 2, inverted = (patternID % 2) == 0;

	// Patterns
	// -----------
	// 00 white
	// 01 black
	// -----------
	// 02 vertical, bit N-0, normal
	// 03 vertical, bit N-0, inverted
	// 04 vertical, bit N-1, normal
	// 04 vertical, bit N-2, inverted
	// ..
	// XX =  (2*_pattern_count + 2) - 2 vertical, bit N, normal
	// XX =  (2*_pattern_count + 2) - 1 vertical, bit N, inverted
	// -----------
	// 2+N+00 = 2*(_pattern_count + 2) horizontal, bit N-0, normal
	// 2+N+01 horizontal, bit N-0, inverted
	// ..
	// YY =  (4*_pattern_count + 2) - 2 horizontal, bit N, normal
	// YY =  (4*_pattern_count + 2) - 1 horizontal, bit N, inverted


	if (patternID < 2) // White or black.
	{
		makePattern(image, numChannels, width, height, vmask, voffset, hmask, hoffset, inverted, whiteValue);
	}
	else if (patternID < 2 * vbits + 2) // Vertical.
	{
		int bit = vbits - patternID / 2;
		vmask = 1 << bit;
		makePattern(image, numChannels, width, height, vmask, voffset, hmask, hoffset, !inverted, whiteValue);
	}
	else if (patternID < 2 * vbits + 2 * hbits + 2) // Horizontal.
	{
		int bit = hbits + vbits - patternID / 2;
		hmask = 1 << bit;
		makePattern(image, numChannels, width, height, vmask, voffset, hmask, hoffset, !inverted, whiteValue);
	}
	else
	{
		return false;
	}

	return true;
}
