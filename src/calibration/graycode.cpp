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




struct vec2b
{
	uint8 x, y;
};


static image<vec2b> estimateDirectLight(const std::vector<image<uint8>>& images, float b)
{
	const uint32 MAX = 10;

	uint32 numImages = (uint32)images.size();
	assert(numImages > 0);

	if (numImages > MAX)
	{
		numImages = MAX;
	}

	image<vec2b> directLight(images[0].width, images[0].height);

	float b1 = 1.f / (1.f - b);
	float b2 = 2.f / (1.f - b * b);

	for (uint32 h = 0; h < images[0].height; ++h)
	{
		const uint8* row[MAX];
		for (uint32 i = 0; i < numImages; ++i)
		{
			row[i] = images[i].data + h * images[i].width;
		}

		for (uint32 w = 0; w < images[0].width; ++w)
		{
			uint32 Lmax = row[0][w];
			uint32 Lmin = row[0][w];
			for (uint32 i = 0; i < numImages; ++i)
			{
				if (Lmax < row[i][w]) Lmax = row[i][w];
				if (Lmin > row[i][w]) Lmin = row[i][w];
			}

			int Ld = (int)(b1 * (Lmax - Lmin) + 0.5f);
			int Lg = (int)(b2 * (Lmin - b * Lmax) + 0.5f);


			vec2b& rowLight = directLight(h, w);
			rowLight.x = (Lg > 0 ? (uint32)Ld : Lmax);
			rowLight.y = (Lg > 0 ? (uint32)Lg : 0);
		}
	}

	return directLight;
}

// https://www.cs.purdue.edu/cgvlab/papers/aliaga/gi07.pdf, See table 2.
static inline uint32 getRobustBit(uint32 value1, uint32 value2, uint32 Ld, uint32 Lg, uint32 m)
{
	if (Ld < m) // direct component is smaller than threshold (i.e. point is not visible from projector pov) -> rule 2
	{
		return BIT_UNCERTAIN;
	}
	if (Ld > Lg) // intervals do not overlap -> rule 1
	{
		return (value1 > value2 ? 1 : 0);
	}
	// if the intervals are overlapping, we can only robustly decide if pixel intensities are not in the overlap region -> rule 3
	if (value1 <= Ld && value2 >= Lg)
	{
		return 0;
	}
	if (value1 >= Lg && value2 <= Ld)
	{
		return 1;
	}
	return BIT_UNCERTAIN;
}

static void convertPattern(image<vec2>& patternImage, const int(&offset)[2], int projWidth, int projHeight)
{
	for (uint32 h = 0; h < patternImage.height; ++h)
	{
		for (uint32 w = 0; w < patternImage.width; ++w)
		{
			vec2& pattern = patternImage(h, w);

			if (validPixel(pattern.x))
			{
				int p = (int)pattern.x;
				int code = grayToBinary(p, offset[0]);

				if (code < 0) { code = 0; }
				else if (code >= projWidth) { code = projWidth - 1; }

				pattern.x = (float)code;
			}
			if (validPixel(pattern.y))
			{
				int p = (int)pattern.y;
				int code = grayToBinary(p, offset[1]);

				if (code < 0) { code = 0; }
				else if (code >= projHeight) { code = projHeight - 1; }

				pattern.y = (float)code;
			}
		}
	}
}

static void removeOutliers(image<vec2>& patternImage, int projWidth, int projHeight)
{
	const float threshold = 10.f;
	const float sqThreshold = threshold * threshold;

	image<vec2> tmp(patternImage);

	for (uint32 y = 1; y < patternImage.height - 1; ++y)
	{
		for (uint32 x = 1; x < patternImage.width - 1; ++x)
		{
			vec2 pattern = tmp(y, x);
			vec2& output = patternImage(y, x);

			if (validPixel(pattern.x, pattern.y))
			{
				vec2 left = tmp(y, x - 1);
				vec2 right = tmp(y, x + 1);
				vec2 top = tmp(y - 1, x);
				vec2 bottom = tmp(y + 1, x);

				int leftOK = !validPixel(left.x, left.y) || (squaredLength(pattern - left) < sqThreshold);
				int rightOK = !validPixel(right.x, right.y) || (squaredLength(pattern - right) < sqThreshold);
				int topOK = !validPixel(top.x, top.y) || (squaredLength(pattern - top) < sqThreshold);
				int bottomOK = !validPixel(bottom.x, bottom.y) || (squaredLength(pattern - bottom) < sqThreshold);

				int allOk = leftOK + rightOK + topOK + bottomOK;
				if (allOk < 4)
				{
					output.x = PIXEL_UNCERTAIN;
					output.y = PIXEL_UNCERTAIN;
				}
			}
		}
	}
}

static bool decodePattern(const std::vector<image<uint8>>& images, image<vec2>& patternImage, image<vec2b>& minMaxImage, const image<vec2b>& directLight, uint32 m, uint32 projWidth, uint32 projHeight)
{
	uint32 totalImages = (uint32)images.size();

	int vbits = 1;
	int hbits = 1;
	for (int i = (1 << vbits); i < (int)projWidth; i = (1 << vbits)) { vbits++; }
	for (int i = (1 << hbits); i < (int)projHeight; i = (1 << hbits)) { hbits++; }
	const int patternOffset[] = { ((1 << vbits) - (int)projWidth) / 2, ((1 << hbits) - (int)projHeight) / 2 };

	uint32 COUNT = 2 + (vbits + hbits) * 2;
	if (totalImages < COUNT)
	{
		return false;
	}

	{
		patternImage.resize(images[0].width, images[0].height);
		minMaxImage.resize(images[0].width, images[0].height);
		patternImage.clearTo(vec2(0.f, 0.f));
		minMaxImage.clearTo({ 255, 0 });

		for (int i = 0; i < vbits + hbits; ++i)
		{
			uint32 imageID1 = 2 + i * 2 + 0;
			uint32 imageID2 = 2 + i * 2 + 1;

			const image<uint8>& image1 = images[imageID1];
			const image<uint8>& image2 = images[imageID2];

			int channel = 0;
			int shift = vbits - i - 1;
			if (i >= vbits)
			{
				channel = 1;
				shift = hbits - (i - vbits) - 1;
			}

			if (image1.width != patternImage.width || image1.height != patternImage.height || image2.width != patternImage.width || image2.height != patternImage.height)
			{
				return false;
			}

			vec2* patternPtr = patternImage.data;
			vec2b* minmaxPtr = minMaxImage.data;
			uint8* image1Ptr = image1.data;
			uint8* image2Ptr = image2.data;
			const vec2b* directLightPtr = directLight.data;

			for (int y = 0; y < (int)patternImage.height; ++y)
			{
				for (int x = 0; x < (int)patternImage.width; ++x)
				{
					vec2& pattern = *patternPtr++;
					vec2b& minMax = *minmaxPtr++;

					uint8 value1 = *image1Ptr++;
					uint8 value2 = *image2Ptr++;

					// min/max
					uint8 minValue = min(value1, value2);
					uint8 maxValue = max(value1, value2);
					minMax.x = min(minValue, minMax.x);
					minMax.y = max(maxValue, minMax.y);

					const vec2b& L = *directLightPtr++;

					uint32 p = getRobustBit(value1, value2, L.x, L.y, m);
					float& patternChannel = pattern.data[channel];
					if (validPixel(patternChannel))
					{
						if (p == BIT_UNCERTAIN)
						{
							patternChannel = PIXEL_UNCERTAIN;
						}
						else
						{
							patternChannel += (p << shift);
						}
					}
				}
			}
		}

		// this assumes that image0 should be brighter than image1
		uint8* image1Ptr = images[0].data;
		uint8* image2Ptr = images[1].data;
		vec2* patternPtr = patternImage.data;

		for (int y = 0; y < (int)patternImage.height; ++y)
		{
			for (int x = 0; x < (int)patternImage.width; ++x)
			{
				uint8 a = *image1Ptr++;
				uint8 b = *image2Ptr++;
				vec2& pattern = *patternPtr++;
				if (b > a)
				{
					pattern = vec2(PIXEL_UNCERTAIN, PIXEL_UNCERTAIN);
				}
			}
		}

	}

	convertPattern(patternImage, patternOffset, projWidth, projHeight);
	removeOutliers(patternImage, projWidth, projHeight);

	return true;
}

bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPixelCorrespondences)
{
	const float b = 0.5f;
	const uint32 m = 100;

	int totalImages = (int)images.size();
	int totalPatterns = totalImages / 2 - 1;
	const int directLightCount = 4;
	const int directLightOffset = 4;

	std::vector<uint32> directComponentImages;
	directComponentImages.reserve(directLightCount * 2);
	for (uint32 i = 0; i < directLightCount; ++i)
	{
		int index = totalImages - totalPatterns - directLightCount - directLightOffset + i + 1;
		directComponentImages.push_back(index);
		directComponentImages.push_back(index + totalPatterns);
	}

	image<vec2b> directLight = estimateDirectLight(images, b);
	image<vec2b> minMaxImage;
	return decodePattern(images, outPixelCorrespondences, minMaxImage, directLight, m, projWidth, projHeight);
}


bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPCImage, std::vector<pixel_correspondence>& outPCVector)
{
	outPCVector.clear();

	if (decodeGraycodeCaptures(images, projWidth, projHeight, outPCImage))
	{
		for (uint32 y = 0; y < outPCImage.height; ++y)
		{
			for (uint32 x = 0; x < outPCImage.width; ++x)
			{
				vec2 proj = outPCImage(y, x);

				if (validPixel(proj))
				{
					vec2 cam = { (float)x, (float)y };
					outPCVector.push_back({ cam, proj });
				}
			}
		}

		return true;
	}
	return false;
}
