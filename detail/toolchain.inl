#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#if defined(_WIN64)
	#include "toolchain_msvc.inl"
#endif

void discover_toolchains(toolchain_map& toolchains)
{
	// FIXME: Actual discovery, detection etc.
	toolchains[msvc::key] = std::make_shared<msvc>();
}
