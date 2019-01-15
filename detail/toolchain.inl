#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#if defined(_WIN64)
	#include "toolchain_msvc.inl"
#endif
#include "toolchain_gcc.inl"

void discover_toolchains(toolchain_map& toolchains)
{
	MTR_SCOPE(__FILE__, __FUNCTION__);
	// FIXME: Actual discovery, detection etc.
#if defined(_WIN64)
	toolchains[msvc::key] = std::make_shared<msvc>();
#endif
	toolchains[gcc::key] = std::make_shared<gcc>();
}

std::string toolchain::get_intermediate_path_for_cpptu(const char *source_path, const char *object_extension, const target &target, const configuration &cfg)
{
	using namespace cbl::path;
	return join(
			get_cppbuild_cache_path(),
			cbl::get_platform_str(cfg.second.platform),
			cfg.first,
			target.first,
			get_path_without_extension(
				get_relative_to(source_path).c_str()) + object_extension);
}
