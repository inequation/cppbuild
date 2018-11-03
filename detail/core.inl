#pragma once

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

constexpr struct
{
	size_t major, minor, patch;
	const bool self_hosted =
#if CPPBUILD_SELF_HOSTED
		true
#else
		false
#endif
		;
} cppbuild_version = { 0, 0, 0 };

enum class error_code : int
{
	success = 0,

	// This is high enough to avoid collisions with system error codes (Windows in particular; see https://docs.microsoft.com/en-us/windows/desktop/debug/system-error-codes).
	first_cppbuild_code = 16000,

	failed_bootstrap_build = first_cppbuild_code,
	failed_bootstrap_bad_toolchain,
	failed_bootstrap_deployment,
	failed_bootstrap_respawn,

	unknown_target,
	unknown_configuration,
};
