#include "../cppbuild.h"
#include "../cbl.h"
#include "toolchain_msvc.h"
#include "toolchain_gcc.h"

void discover_toolchains(toolchain_map& toolchains)
{
	MTR_SCOPE_FUNC();
	std::shared_ptr<toolchain> tc;
	// FIXME: Use parallel_for.
#if defined(_WIN64)
	tc = std::make_shared<msvc>();
	if (tc->initialize())
		toolchains[msvc::key] = tc;
#endif
	tc = std::make_shared<gcc>();
	if (tc->initialize())
		toolchains[gcc::key] = tc;
	if (toolchains.empty())
	{
		cbl::error("No toolchains discovered. Check verbose log for details.");
		abort();
	}
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
