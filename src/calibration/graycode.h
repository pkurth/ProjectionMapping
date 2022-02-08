#pragma once

#include "core/math.h"
#include "core/image.h"

struct pixel_correspondence
{
	vec2 camera;
	vec2 projector;
};

constexpr uint32 BIT_UNCERTAIN = -1;
constexpr float PIXEL_UNCERTAIN = -1.f;

static bool validPixel(float p)
{
	return p != PIXEL_UNCERTAIN;
}

static bool validPixel(vec2 p)
{
	return validPixel(p.x) && validPixel(p.y);
}

static bool validPixel(float p1, float p2)
{
	return validPixel(p1) && validPixel(p2);
}



uint32 getNumberOfGraycodePatternsRequired(uint32 width, uint32 height);
bool generateGraycodePattern(uint8* image, uint32 width, uint32 height, uint32 patternID, uint8 whiteValue);

bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPixelCorrespondences);
bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPCImage, std::vector<pixel_correspondence>& outPCVector);
