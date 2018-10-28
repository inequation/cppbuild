#pragma once

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
#else
	#error Unsupported platform
#endif

#include <string>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <ctime>

typedef std::vector<std::string> string_vector;
