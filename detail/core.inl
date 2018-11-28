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
		uint16_t major, minor, build, revision;
		char tag[64];

		bool operator==(const version& other) const { return 0 == memcmp(this, &other, sizeof(*this)); }
		bool operator<(const version& other) const
		{
			return major < other.major || (major == other.major &&
					minor < other.minor || (minor == other.minor &&
						build < other.build || (build == other.build &&
							revision < other.revision)));
		}
		bool parse(const char *str)
		{
			revision = build = minor = major = 0;
			tag[0] = 0;
			size_t scanned = sscanf(str, "%hu.%hu.%hu.%hu", &major, &minor, &build, &revision);
			if (scanned)
			{
				const char *minus = strchr(str, '-');
				if (minus)
					strncpy(tag, minus - 1, std::min(strlen(minus), sizeof(tag) - 1));
				return true;
			}
			return false;
		}
		std::string to_string() const
		{
			std::string str;
			str.resize(4 * 5 + 3 * 1 + sizeof(tag));
			if (int written = sprintf(
				(char *)str.data(),
				"%hu.%hu.%hu.%hu%s%s",
				major, minor, build, revision, tag[0] ? "-" : "", tag
			))
			{
				str.resize(written);
			}
			else
				str.clear();
			return str;
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
			return combine(hasher(v.major), combine(hasher(v.minor), combine(hasher(v.build), hasher(v.revision))));
		}
	};
}

#ifndef CPPBUILD_GENERATION
	#define CPPBUILD_GENERATION	0
#endif
#define CPPBUILD_STRINGIFY(x)	CPPBUILD_STRINGIFY2(x)
#define CPPBUILD_STRINGIFY2(x)	#x
constexpr cbl::version cppbuild_version = { 0, 0, 0, 0,
#if CPPBUILD_GENERATION
		"gen" CPPBUILD_STRINGIFY(CPPBUILD_GENERATION)
#else
		"gen1"
#endif
};

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
