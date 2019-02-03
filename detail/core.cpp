#include "../cppbuild.h"
#include "../cbl.h"
#include "detail.h"

#if !defined(_GNU_SOURCE) && !defined(_BSD_SOURCE)
	#include "getopt/getopt.h"
#else
	#include <unistd.h>
	#include <getopt.h>
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

void print_version()
{
	cbl::info("cppbuild version %s (" __DATE__ ", " __TIME__ ")", cppbuild_version.to_string().c_str());
}

cppbuild::options g_options;

const std::string &get_optstring()
{
	static std::string optstring;
	if (optstring.empty())
	{
		for (const auto &op : g_options)
		{
			if (op.short_opt)
				optstring += op.short_opt;
			if (op.arg == cppbuild::option::arg_required)
				optstring += ':';
			else if (op.arg == cppbuild::option::arg_optional)
				optstring += "::";
		}
	}
	return optstring;
}

const struct option *get_long_options()
{
	static struct option long_options[cppbuild::options::count + 1] = { { 0 } };
	if (!long_options[0].name)
	{
		size_t index = 0;
		for (const auto &opt : g_options)
		{
			if (opt.long_opt)
			{
				long_options[index].name = opt.long_opt;
				if (opt.arg == cppbuild::option::arg_required)
					long_options[index].has_arg = required_argument;
				else if (opt.arg == cppbuild::option::arg_optional)
					long_options[index].has_arg = optional_argument;
				else
					long_options[index].has_arg = no_argument;
				long_options[index].flag = nullptr;
				long_options[index].val = -int(2 + &opt - g_options.begin());
				++index;
			}
		}
	}
	return long_options;
}

void print_usage(const char *argv0)
{
	print_version();
	using namespace cbl;
	info("Usage: %s [options] [target] [configuration]", argv0);
	std::string opt_str;
	const char *arg;
	for (const auto &opt : g_options)
	{
		if (opt.desc)
		{
			opt_str = "";
			constexpr static const char *arg[] =
			{
				"",
				"[arg]",
				"<arg>"
			};
			if (opt.short_opt)
			{
				opt_str += '-';
				opt_str += opt.short_opt;
				if (opt.arg != cppbuild::option::arg_none)
				{
					opt_str += ' ';
					opt_str += arg[opt.arg];
				}
			}
			if (opt.long_opt)
			{
				if (opt.short_opt)
					opt_str += ", ";
				opt_str += "--";
				opt_str += opt.long_opt;
				if (opt.arg != cppbuild::option::arg_none)
				{
					opt_str += '=';
					opt_str += arg[opt.arg];
				}
			}
			
			info("\t%s\t%s", opt_str.c_str(), opt.desc);
		}
	}
}

int parse_args(int &argc, char **&argv)
{
	int c;
	int digit_optind = 0;

	const auto *optstring = get_optstring().c_str();
	const auto *long_options = get_long_options();

	// We handle our own errors.
	opterr = 0;

	while (true)
	{
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;

		c = getopt_long(argc, argv, optstring, long_options, &option_index);

		if (c == -1)
			break;
		else if (c == '?')
		{
			cbl::error("Unknown option '%s'.", argv[optind - 1]);
			print_usage(argv[0]);
			exit(1);
		}

		cppbuild::option *opt = nullptr;
		if (c < -1)
			opt = &g_options[-c - 2];
		else
		{
			for (auto &o : g_options)
			{
				if (c == o.short_opt)
				{
					opt = &o;
					break;
				}
			}
		}

		assert(opt);

		using namespace cppbuild;

		switch (opt->arg)
		{
		case cppbuild::option::arg_none:
			assert(opt->type == cppbuild::option::boolean);
			opt->val.as_bool = !opt->default_val.as_bool;
			break;
		case cppbuild::option::arg_optional:
			if (!optarg)
			{
				opt->val = opt->default_val;
				break;
			}
			// Intentional fall-through.
		case cppbuild::option::arg_required:
			assert(optarg);
			switch (opt->type)
			{
			case cppbuild::option::boolean:
				opt->val.as_bool = !!atoi(optarg);
				break;
			case cppbuild::option::int32:
				opt->val.as_int32 = atoi(optarg);
				break;
			case cppbuild::option::int64:
				opt->val.as_int64 = strtoll(optarg, nullptr, 0);
				break;
			case cppbuild::option::str_ptr:
				opt->val.as_str_ptr = optarg;
				break;
			}
			break;
		default: assert(!"Unsupported argument setting");
		}
	}

	return optind;
}
