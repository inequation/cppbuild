#pragma once

#if !defined(__linux__) || !defined(__x86_64__)
	#error Only include this file on 64-bit Linux
#endif

#include "../cppbuild.h"

namespace cbl
{
	namespace path
	{
		constexpr const char get_path_separator() { return '/'; }
		constexpr const char get_alt_path_separator() { return get_path_separator(); }
	}

	constexpr platform get_host_platform()
	{
		return platform::linux64;
	}
}
