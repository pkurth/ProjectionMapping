#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <tchar.h>

#include <cassert>
#include <limits>
#include <array>
#include <string>
#include <vector>
#include <iostream>
#include <memory>

#include <filesystem>
namespace fs = std::filesystem;

#include <mutex>

#include <wrl.h> 

typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;
typedef wchar_t wchar;

template <typename T> using ref = std::shared_ptr<T>;
template <typename T> using weakref = std::weak_ptr<T>;

template <typename T, class... Args>
inline ref<T> make_ref(Args&&... args) 
{ 
	return std::make_shared<T>(std::forward<Args>(args)...); 
}

template <typename T>
using com = Microsoft::WRL::ComPtr<T>;

#define arraysize(arr) (sizeof(arr) / sizeof((arr)[0]))

template <typename T>
static constexpr auto min(T a, T b) { return a < b ? a : b; }
template <typename T>
static constexpr auto max(T a, T b) { return a > b ? a : b; }

#define setBit(mask, bit) mask |= (1 << (bit))
#define unsetBit(mask, bit) mask ^= (1 << (bit))

static void checkResult(HRESULT hr)
{
	assert(SUCCEEDED(hr));
}



