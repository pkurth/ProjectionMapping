#include "pch.h"
#include "calibration_image.h"




enum manhattan_direction : uint8_t
{
	manhattan_none,
	manhattan_vertical,
	manhattan_horizontal,
	manhattan_diagonal,
};

static image<int16> manhattanDistanceAllowDiagonal(const image<uint8>& img, uint8 search)
{
	image<int16> manhattan(img.width, img.height);
	image<manhattan_direction> direction(img.width, img.height);

	for (uint32 y = 0; y < img.height; ++y)
	{
		for (uint32 x = 0; x < img.width; ++x)
		{
			if (img(y, x) == search)
			{
				manhattan(y, x) = 0;
				direction(y, x) = manhattan_none;
			}
			else
			{
				int16 valHere = img.height + img.width;
				manhattan_direction dirHere = manhattan_none;

				if (y > 0)
				{
					int16 top = manhattan(y - 1, x);
					manhattan_direction dirTop = direction(y - 1, x);
					int16 newVal = top + 1;
					manhattan_direction newDir = manhattan_vertical;
					if (dirTop == manhattan_horizontal)
					{
						--newVal;
						newDir = manhattan_diagonal;
					}
					if (newVal < valHere)
					{
						valHere = newVal;
						dirHere = newDir;
					}
				}
				if (x > 0)
				{
					int16 left = manhattan(y, x - 1);
					manhattan_direction dirLeft = direction(y, x - 1);
					int16 newVal = left + 1;
					manhattan_direction newDir = manhattan_horizontal;
					if (dirLeft == manhattan_vertical)
					{
						--newVal;
						newDir = manhattan_diagonal;
					}
					if (newVal < valHere)
					{
						valHere = newVal;
						dirHere = newDir;
					}
				}
				if (x > 0 && y > 0)
				{
					int16 topLeft = manhattan(y - 1, x - 1);
					int16 newVal = topLeft + 1;
					if (newVal < valHere)
					{
						valHere = newVal;
						dirHere = manhattan_diagonal;
					}
				}
				if (x < img.width - 1 && y > 0)
				{
					int16 topRight = manhattan(y - 1, x + 1);
					int16 newVal = topRight + 1;
					if (newVal < valHere)
					{
						valHere = newVal;
						dirHere = manhattan_diagonal;
					}
				}

				manhattan(y, x) = valHere;
				direction(y, x) = dirHere;
			}
		}
	}
	for (int32 y = (int32)img.height - 1; y >= 0; --y)
	{
		for (int32 x = (int32)img.width - 1; x >= 0; --x)
		{
			int16 valHere = manhattan(y, x);
			manhattan_direction dirHere = direction(y, x);

			if (y < (int32)img.height - 1)
			{
				int16 bottom = manhattan(y + 1, x);
				manhattan_direction dirBottom = direction(y + 1, x);
				int16 newVal = bottom + 1;
				manhattan_direction newDir = manhattan_vertical;
				if (dirBottom == manhattan_horizontal)
				{
					--newVal;
					newDir = manhattan_diagonal;
				}
				if (newVal < valHere)
				{
					valHere = newVal;
					dirHere = newDir;
				}
			}
			if (x < (int32)img.width - 1)
			{
				int16 right = manhattan(y, x + 1);
				manhattan_direction dirRight = direction(y, x + 1);
				int16 newVal = right + 1;
				manhattan_direction newDir = manhattan_horizontal;
				if (dirRight == manhattan_vertical)
				{
					--newVal;
					newDir = manhattan_diagonal;
				}
				if (newVal < valHere)
				{
					valHere = newVal;
					dirHere = newDir;
				}
			}
			if (x < (int32)img.width - 1 && y < (int32)img.height - 1)
			{
				int16 bottomRight = manhattan(y + 1, x + 1);
				int16 newVal = bottomRight + 1;
				if (newVal < valHere)
				{
					valHere = newVal;
					dirHere = manhattan_diagonal;
				}
			}
			if (x > 0 && y < (int32)img.height - 1)
			{
				int16 bottomLeft = manhattan(y + 1, x - 1);
				int16 newVal = bottomLeft + 1;
				if (newVal < valHere)
				{
					valHere = newVal;
					dirHere = manhattan_diagonal;
				}
			}

			manhattan(y, x) = valHere;
			direction(y, x) = dirHere;
		}
	}
	return manhattan;
}

static image<int16> manhattanDistanceCross(const image<uint8>& img, uint8 search)
{
	image<int16> manhattan(img.width, img.height);
	for (int32 y = 0; y < (int32)img.height; ++y)
	{
		for (int32 x = 0; x < (int32)img.width; ++x)
		{
			if (img(y, x) == search)
			{
				manhattan(y, x) = 0;
			}
			else
			{
				manhattan(y, x) = img.height + img.width;
				if (y > 0)
				{
					manhattan(y, x) = min(manhattan(y, x), (int16)(manhattan(y - 1, x) + 1));
				}
				if (x > 0)
				{
					manhattan(y, x) = min(manhattan(y, x), (int16)(manhattan(y, x - 1) + 1));
				}
			}
		}
	}
	for (int y = (int32)img.height - 1; y >= 0; --y)
	{
		for (int x = (int32)img.width - 1; x >= 0; --x)
		{
			if (y < (int32)img.height - 1)
			{
				manhattan(y, x) = min(manhattan(y, x), (int16)(manhattan(y + 1, x) + 1));
			}
			if (x < (int32)img.width - 1)
			{
				manhattan(y, x) = min(manhattan(y, x), (int16)(manhattan(y, x + 1) + 1));
			}
		}
	}
	return manhattan;
}

void dilate(image<uint8>& img, int numIterations, bool allowDiagonal)
{
	image<int16> manhattan = allowDiagonal ? manhattanDistanceAllowDiagonal(img, 255) : manhattanDistanceCross(img, 255);
	for (uint32 y = 0; y < img.height; ++y)
	{
		for (uint32 x = 0; x < img.width; ++x)
		{
			img(y, x) = (manhattan(y, x) <= numIterations) ? 255 : 0;
		}
	}
}

void erode(image<uint8>& img, int numIterations, bool allowDiagonal)
{
	image<int16> manhattan = allowDiagonal ? manhattanDistanceAllowDiagonal(img, 0) : manhattanDistanceCross(img, 0);
	for (uint32 y = 0; y < img.height; ++y)
	{
		for (uint32 x = 0; x < img.width; ++x)
		{
			img(y, x) = (manhattan(y, x) <= numIterations) ? 0 : 255;
		}
	}
}
