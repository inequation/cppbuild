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

namespace cbl
{
	struct version
	{
		size_t major, minor, patch;

		bool operator==(const version& other) const { return 0 == memcmp(this, &other, sizeof(*this)); }
		bool operator<(const version& other) const
		{
			return major < other.major || (major == other.major &&
					minor < other.minor || (minor == other.minor &&
						patch < other.patch));
		}
	};
}

namespace std
{
	template <> struct hash<cbl::version>
	{
		size_t operator()(const cbl::version &v) const
		{
			hash<size_t> hasher;
			auto combine = [](size_t a, size_t b) { return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2)); };
			return combine(hasher(v.major), combine(hasher(v.minor), hasher(v.patch)));
		}
	};
}

constexpr struct cppbuild_version
{
	cbl::version number;
	const bool self_hosted =
#if CPPBUILD_SELF_HOSTED
		true
#else
		false
#endif
		;
} cppbuild_version = { { 0, 0, 0 } };

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
