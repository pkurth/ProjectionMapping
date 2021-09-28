#define MAIN_DIM		x
#define OTHER_DIM		y
#define NUM_THREADS		DILATION_BLOCK_SIZE,1

#include "dilation_common.hlsli"

