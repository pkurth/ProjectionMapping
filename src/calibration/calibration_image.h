#pragma once

#include "core/math.h"

struct pixel_correspondence
{
	vec2 camera;
	vec2 projector;
};

template <typename type>
struct image
{
	type* data = nullptr;
	uint32 width;
	uint32 height;

	bool ownership;

	image()
		: width(0), height(0), data(nullptr), ownership(false)
	{
	}

	image(uint32 width, uint32 height)
		: width(width), height(height), ownership(true)
	{
		data = new type[width * height];
	}

	image(uint32 width, uint32 height, type* data)
		: width(width), height(height), data(data), ownership(false)
	{
	}

	image(const image& i)
		: width(i.width), height(i.height)
	{
		if (i.data)
		{
			data = new type[width * height];
			memcpy(data, i.data, width * height * sizeof(type));
			ownership = true;
		}
		else
		{
			data = nullptr;
			ownership = false;
		}
	}

	image(image&& i)
		: width(i.width), height(i.height)
	{
		data = i.data;
		i.data = nullptr;

		ownership = i.ownership;
	}

	~image()
	{
		width = height = 0;
		if (data && ownership)
		{
			delete[] data;
		}
		data = nullptr;
	}

	image<type>& operator=(const image<type>& i)
	{
		resize(i.width, i.height);
		if (i.data)
		{
			memcpy(data, i.data, width * height * sizeof(type));
		}

		return *this;
	}

	image<type>& operator=(image<type>&& i)
	{
		std::swap(width, i.width);
		std::swap(height, i.height);
		std::swap(data, i.data);
		std::swap(ownership, i.ownership);

		return *this;
	}

	inline void clearTo(type t)
	{
		if (data)
		{
			for (uint32 i = 0; i < width * height; ++i)
			{
				data[i] = t;
			}
		}
	}

	inline void resize(int width, int height)
	{
		if (width != this->width || height != this->height)
		{
			this->width = width;
			this->height = height;
			if (data && ownership) delete[] data;
			data = new type[width * height];
			ownership = true;
		}
	}

	template <typename other_type, typename transformation_func>
	inline void convertFrom(const image<other_type>& i, transformation_func func)
	{
		resize(i.width, i.height);
		for (uint32 y = 0; y < height; ++y)
		{
			for (uint32 x = 0; x < width; ++x)
			{
				data[y * width + x] = func(i(y, x));
			}
		}
	}

	template <typename other_type1, typename other_type2, typename transformation_func>
	inline void convertFrom(const image<other_type1>& i1, const image<other_type2>& i2, transformation_func func)
	{
		assert(i1.width == i2.width);
		assert(i1.height == i2.height);

		resize(i1.width, i1.height);
		for (uint32 y = 0; y < height; ++y)
		{
			for (uint32 x = 0; x < width; ++x)
			{
				data[y * width + x] = func(i1(y, x), i2(y, x));
			}
		}
	}

	template <typename transformation_func>
	inline void forEach(transformation_func func)
	{
		for (uint32 i = 0; i < width * height; ++i)
		{
			func(data[i]);
		}
	}

	inline type& operator()(uint32 y, uint32 x)
	{
		return data[y * width + x];
	}

	inline const type& operator()(uint32 y, uint32 x) const
	{
		return data[y * width + x];
	}

	inline type& operator()(int32 y, int32 x)
	{
		return data[y * width + x];
	}

	inline const type& operator()(int32 y, int32 x) const
	{
		return data[y * width + x];
	}

	inline type operator()(float y, float x) const
	{
		uint32 iy = (uint32)y;
		uint32 ix = (uint32)x;
		float fy = y - iy;
		float fx = x - ix;

		uint32 iy1 = clamp(iy + 1, 0u, height - 1);
		uint32 ix1 = clamp(ix + 1, 0u, width - 1);

		type tl = (*this)(iy, ix);
		type tr = (*this)(iy, ix1);
		type bl = (*this)(iy1, ix);
		type br = (*this)(iy1, ix1);

		type t = lerp(tl, tr, fx);
		type b = lerp(bl, br, fx);
		type result = lerp(t, b, fy);

		return result;
	}

	inline void flipVertically()
	{
		for (uint32 y = 0; y < height / 2; ++y)
		{
			type* row0 = data + (width * y);
			type* row1 = data + (width * (height - 1 - y));

			for (uint32 x = 0; x < width; ++x)
			{
				std::swap(row0[x], row1[x]);
			}
		}
	}
};

void dilate(image<uint8>& img, int numIterations, bool allowDiagonal);
void erode(image<uint8>& img, int numIterations, bool allowDiagonal);

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


