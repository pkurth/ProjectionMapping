#include "core/math.h"
#include "calibration_internal.h"

mat3 computeFundamentalMatrix(const std::vector<pixel_correspondence>& pc, std::vector<uint8>& outMask);
