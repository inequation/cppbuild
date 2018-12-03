#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#if defined(_WIN64)
	#include "toolchain_msvc.inl"
#endif
#include "toolchain_gcc.inl"

void discover_toolchains(toolchain_map& toolchains)
{
	// FIXME: Actual discovery, detection etc.
#if defined(_WIN64)
	toolchains[msvc::key] = std::make_shared<msvc>();
#endif
	toolchains[gcc::key] = std::make_shared<gcc>();
}
