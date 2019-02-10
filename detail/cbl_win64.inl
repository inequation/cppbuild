#pragma once

#if !defined(_WIN64)
	#error Only include this file on 64-bit Windows
#endif

#include "../cppbuild.h"

namespace cbl
{
	namespace path
	{
		constexpr const char get_path_separator() { return '\\'; }
		constexpr const char get_alt_path_separator() { return '/'; }
	}

	constexpr platform get_host_platform()
	{
		return platform::win64;
	}
}
