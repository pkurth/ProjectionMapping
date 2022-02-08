#include "core/math.h"
#include "math_double.h"
#include "graycode.h"

mat3d computeFundamentalMatrix(const std::vector<pixel_correspondence>& pc, std::vector<uint8>& outMask);
