#include "../cppbuild.h"

#if !defined(_GNU_SOURCE) && !defined(_BSD_SOURCE)
	#include "getopt/getopt.h"
#endif

bool version::operator==(const version& other) const
{
	return 0 == memcmp(this, &other, sizeof(*this));
}

bool version::operator<(const version& other) const
{
	return major < other.major || (major == other.major &&
			(minor < other.minor || (minor == other.minor &&
				(build < other.build || (build == other.build &&
					revision < other.revision)))));
}

bool version::parse(const char *str)
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

std::string version::to_string() const
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

#if !defined(_GNU_SOURCE) && !defined(_BSD_SOURCE)
	#include "getopt/getopt.c"
	#include "getopt/getopt_long.c"
#endif
