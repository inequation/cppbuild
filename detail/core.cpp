#include "../cppbuild.h"
#include "../cbl.h"
#include "detail.h"

// We always use our bundled FreeBSD implementation, because some getopt_long() implementations lie about
// support for optional arguments to short options (looking at you, Debian!).
#include "getopt/getopt.h"

#include <chrono>
#include <thread>

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

#define CPPBUILD_STRINGIFY(x)	CPPBUILD_STRINGIFY2(x)
#define CPPBUILD_STRINGIFY2(x)	#x
const version cppbuild_version = { 0, 0, 0, 0,
#if CPPBUILD_GENERATION
	"gen" CPPBUILD_STRINGIFY(CPPBUILD_GENERATION)
#else
	"gen1"
#endif
};
#undef CPPBUILD_STRINGIFY2
#undef CPPBUILD_STRINGIFY

void print_version()
{
	cbl::info("cppbuild version %s %s (" __DATE__ ", " __TIME__ ")",
		cppbuild_version.to_string().c_str(),
		cbl::get_host_platform_str());
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
			{
				optstring += op.short_opt;
				if (op.arg == cppbuild::option::arg_required)
					optstring += ':';
				else if (op.arg == cppbuild::option::arg_optional)
					optstring += "::";
			}
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
				long_options[index].val = 0;
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
	info("Options:");
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

static int internal_parse_args(int argc, const char **argv, bool ignore_non_defaults)
{
	int c;

	const auto *optstring = get_optstring().c_str();
	const auto *long_options = get_long_options();

	// We handle our own errors.
	opterr = 0;

	while (true)
	{
		int option_index = 0;

		c = getopt_long(argc, const_cast<char **>(argv), optstring, long_options, &option_index);

		if (c == -1)
			break;
		else if (c == '?')
			return -1;
		else if (c == ':')
		{
			// The user messed up, show usage message.
			g_options.help.val.as_bool = true;
			return optind;
		}

		cppbuild::option *opt = nullptr;
		if (c == 0)
			opt = &g_options[option_index];
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

		if (!ignore_non_defaults || opt->val == opt->default_val)
		{
			switch (opt->arg)
			{
			case cppbuild::option::arg_none:
				assert(opt->type == cppbuild::option::boolean);
				opt->val.as_bool = !opt->default_val.as_bool;
				break;
			case cppbuild::option::arg_optional:
				if (!optarg)
				{
					switch (opt->type)
					{
					case cppbuild::option::boolean:
						opt->val.as_bool = !opt->default_val.as_bool;
						break;
					case cppbuild::option::int32:
						// TODO: Perhaps another "default value for optional arg" is called for?
						opt->val.as_int32 = !opt->default_val.as_int32;
						break;
					case cppbuild::option::int64:
						// TODO: Perhaps another "default value for optional arg" is called for?
						opt->val.as_int64 = !opt->default_val.as_int64;
						break;
					case cppbuild::option::str_ptr:
						// Huh?
						assert(!"String options cannot have optional arguments");
						break;
					}
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
	}

	return optind;
}

int parse_args(int argc, const char **argv)
{
	int first_non_opt_arg = internal_parse_args(argc, argv, false);
	if (first_non_opt_arg < 0)
	{
		cbl::error("Unknown option '%s'.", argv[optind - 1]);
		print_usage(argv[0]);
		exit(1);
	}
	return first_non_opt_arg;
}

void override_options(const string_vector &args)
{
#if CPPBUILD_BSD_GETOPT
	optreset = 1;
#endif
	optind = 0;

	auto *argv = new const char *[1 + args.size()];
	auto *p = argv;
	*(p++) = "build";
	for (auto &a : args)
		*(p++) = a.c_str();
	
	internal_parse_args(1 + args.size(), argv, true);

	delete [] argv;
}

namespace cppbuild
{
	using namespace cbl;

	background_delete::worker::worker(const string_vector &list_)
		: enki::ITaskSet(list_.size(), 100)
		, list(list_)
	{}

	void background_delete::worker::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
	{
		MTR_SCOPE(__FILE__, "Background deletion");
		for (auto i = range.start; i < range.end; ++i)
		{
			if (!fs::delete_file(list[i].c_str()))
				warning("Failed to delete old log file %s", list[i].c_str());
		}
	}

	background_delete::background_delete()
		: log_dir(path::join(path::get_cppbuild_cache_path(), "log"))
	{}

	void background_delete::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
	{
		string_vector to_delete;

		static const char *globs[] = { "*.log", "*.json" };
		for (const char *glob : globs)
		{
			auto old_logs = cbl::fs::enumerate_files(cbl::path::join(log_dir, glob).c_str());
			const size_t max_old_log_files = g_options.rotate_log_count.val.as_int64;
			if (old_logs.size() > max_old_log_files)
			{
				std::sort(old_logs.begin(), old_logs.end(),
					[](const std::string& a, const std::string& b)
				{
					return fs::get_modification_timestamp(a.c_str())
						< fs::get_modification_timestamp(b.c_str());
				});
				to_delete.insert(to_delete.end(), old_logs.begin(), old_logs.end() - max_old_log_files);
			}
		}

		if (!to_delete.empty())
		{
			worker task(to_delete);
			cbl::scheduler.AddTaskSetToPipe(&task);
			cbl::scheduler.WaitforTask(&task);
		}
	}
}

static void rotate(const char *log_dir, const char *ext)
{
	using namespace cbl;
	fs::mkdir(log_dir, true);
	std::string file = path::join(log_dir, std::string("cppbuild.") + ext);
	uint64_t stamp = fs::get_modification_timestamp(file.c_str());
	if (stamp != 0)
	{
		int y, M, d, h, m, s, us;
		time::of_day(stamp, &y, &M, &d, &h, &m, &s, &us);
		char new_name[36];
		std::string old_file = path::get_path_without_extension(file.c_str()) + '-';
		uint64_t number = uint64_t(y) * 10000 + uint64_t(M) * 100 + uint64_t(d);
		old_file += std::to_string(number) + '-';
		number = uint64_t(h) * 10000 + uint64_t(m) * 100 + uint64_t(s);
		old_file += std::to_string(number) + '-';
		old_file += std::to_string(us) + '.' + ext;
		if (!fs::move_file(file.c_str(), old_file.c_str(), fs::maintain_timestamps))
		{
			warning("Failed to rotate %s file %s to %s", ext, file.c_str(), old_file.c_str());
		}
	}
}

// Exponential back-off until the process holding the file terminates.
static FILE *fopen_with_exponential_backoff(const char *path, const char *mode, int attempts = 10)
{
	FILE *result = nullptr;
	auto duration = std::chrono::microseconds(50);
	for (int i = 0; i < attempts; ++i)
	{
		result = fopen(path, mode);
		if (result)
		{
			break;
		}
		else
		{
			std::this_thread::sleep_for(duration);
			duration *= 2;
		}
	}
	return result;
}

namespace cbl
{
	namespace detail
	{
		extern FILE *log_file_stream;
		extern FILE *trace_file_stream;
	};
};

void rotate_traces(bool append_to_current)
{
#if MTR_ENABLED
	using namespace cbl;
	using namespace cbl::detail;

	// Rotate the latest log file to a sortable, timestamped format.
	std::string log_dir = path::join(path::get_cppbuild_cache_path(), "log");
	std::string log = path::join(log_dir, "cppbuild.json");

	long file_end = 0;
	if (append_to_current)
	{
		// NOTE: This appending implementation depends heavily on the knowledge of minitrace's
		// internals and Chrome trace JSON structure. We overwrite the footer of the old trace, as
		// well as the header of the new trace, and achieve concatenation of all related
		// invocations in the same file.
		// If this stream were opened with "a" mode, all outputs would end up at the end of the
		// file and seeking operations would be silently ignored. Instead, we want to retain the
		// ability to seek. On the other hand, "r+" mode may succeed while the file is still open
		// for writing. Therefore, we open it twice: first just to wait, second to actually use it.
		trace_file_stream = fopen_with_exponential_backoff(log.c_str(), "ab");
		if (trace_file_stream)
		{
			fclose(trace_file_stream);
			trace_file_stream = fopen(log.c_str(), "r+b");
			fseek(trace_file_stream, 0, SEEK_END);
			file_end = ftell(trace_file_stream);
		}
	}
	if (!trace_file_stream)
	{
		rotate(log_dir.c_str(), "json");

		trace_file_stream = fopen(log.c_str(), "wb");
	}
	// Make sure that handle inheritance doesn't block log rotation in the deploying child process.
	cbl::fs::disinherit_stream(trace_file_stream);

	mtr_init_from_stream(trace_file_stream);
	atexit(mtr_shutdown);

	if (file_end != 0)
	{
		// Rewind past the new header and the old footer, so that new events overwrite both.
		// NOTE: The resulting JSON would be malformed if fewer event characters were written to
		// it than the sum of lengths of the footer and header, but thankfully, the metas below
		// do the job just fine.
		fseek(trace_file_stream, file_end - (int)strlen("\n]}\n"), SEEK_SET);
		fputs(",\n", trace_file_stream);
	}

	MTR_META_PROCESS_NAME(append_to_current ? "cppbuild (restarted)" : "cppbuild");
	MTR_META_THREAD_NAME("Main Thread");

	// Also register with the scheduler callbacks.
	auto callbacks = cbl::scheduler.GetProfilerCallbacks();
	callbacks->threadStart = [](uint32_t thread_index)
	{
		MTR_META_THREAD_NAME(("Task " + std::to_string(thread_index)).c_str());
		MTR_META_THREAD_SORT_INDEX((uintptr_t)(thread_index + 1));
	};
	callbacks->threadStop = nullptr;
	callbacks->waitStart = [](uint32_t thread_index) { MTR_BEGIN(__FILE__, "Wait"); };
	callbacks->waitStop = [](uint32_t thread_index) { MTR_END(__FILE__, "Wait"); };
#endif	// MTR_ENABLED
}

void rotate_logs(bool append_to_current)
{
	using namespace cbl;
	using namespace cbl::detail;

	// Rotate the latest log file to a sortable, timestamped format.
	std::string log_dir = path::join(path::get_cppbuild_cache_path(), "log");
	std::string log = path::join(log_dir, "cppbuild.log");
	if (append_to_current)
	{
		log_file_stream = fopen_with_exponential_backoff(log.c_str(), "a");
	}
	if (!log_file_stream)
	{
		rotate(log_dir.c_str(), "log");

		// Open the new stream.
		log_file_stream = fopen(log.c_str(), "w");
	}
	// Make sure that handle inheritance doesn't block log rotation in the deploying child process.
	cbl::fs::disinherit_stream(log_file_stream);
	atexit([]() { fclose(log_file_stream); });
}
