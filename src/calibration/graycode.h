#pragma once

#include "calibration_image.h"

uint32 getNumberOfGraycodePatternsRequired(uint32 width, uint32 height);
bool generateGraycodePattern(uint8* image, uint32 width, uint32 height, uint32 patternID, uint8 whiteValue);

bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPixelCorrespondences);
bool decodeGraycodeCaptures(const std::vector<image<uint8>>& images, uint32 projWidth, uint32 projHeight, image<vec2>& outPCImage, std::vector<pixel_correspondence>& outPCVector);
