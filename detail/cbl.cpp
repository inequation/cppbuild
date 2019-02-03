#include "../cppbuild.h"

#include <cstdarg>
#include <cctype>
#include <mutex>
#include <thread>
#include <sstream>
#include <chrono>
#include "../cbl.h"
#include "cbl_detail.h"
#include "detail.h"

namespace cbl
{
	constexpr const char* get_default_toolchain_for_host()
	{
		constexpr const char *toolchain_names[] =
		{
			"msvc",	// win64
			"gcc"	// linux
		};
		return toolchain_names[(uint8_t)get_host_platform()];
	}

	constexpr const char *get_platform_str(platform p)
	{
#define IF_ENUM_STR(x)	if (p == platform::x) return #x;
		IF_ENUM_STR(win64)
		else IF_ENUM_STR(linux64)
		else IF_ENUM_STR(macos)
		else IF_ENUM_STR(ps4)
		else IF_ENUM_STR(xbox1)
		else return (assert(!"Unknown platform"), "unknown");
#undef IF_ENUM_STR
	}

	constexpr const char *get_host_platform_str()
	{
		return get_platform_str(get_host_platform());
	}

	size_t combine_hash(size_t a, size_t b)
	{
		return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
	};

	namespace path
	{
		string_vector split(const char *path)
		{
			string_vector elements;
			size_t len = strlen(path);
			const char *begin = path;
			for (size_t i = 0; i < len; ++i)
			{
				if (path + i < begin)
				{
					continue;
				}
				if (is_path_separator(path[i]))
				{
					if (i == 0)
					{
						continue;
					}

					std::string new_element(begin, path + i - begin);
					if (!elements.empty() && elements.back().length() == 0)
					{
						elements.back() = std::move(new_element);
					}
					else
					{
						elements.push_back(new_element);
					}

					begin = path + i + 1;
				}
			}
			if (*begin)
			{
				elements.push_back(begin);
			}
			return elements;
		}

		std::string get_extension(const char *path)
		{
			if (const char* dot = strrchr(path, '.'))
			{
				return std::string(dot + 1);
			}
			else
			{
				return std::string();
			}
		}

		std::string get_path_without_extension(const char *path)
		{
			if (const char* dot = strrchr(path, '.'))
			{
				return std::string(path, dot - path);
			}
			else
			{
				return std::string(path);
			}
		}

		std::string get_directory(const char *path)
		{
			if (const char* sep = strrchr(path, get_path_separator()))
			{
				return std::string(path, sep - path);
			}
			else if (const char* sep = strrchr(path, get_alt_path_separator()))
			{
				return std::string(path, sep - path);
			}
			else
			{
				return std::string(path);
			}
		}

		std::string get_filename(const char *path)
		{
			if (const char* sep = strrchr(path, get_path_separator()))
			{
				return std::string(sep + 1);
			}
			else if (const char* sep = strrchr(path, get_alt_path_separator()))
			{
				return std::string(sep + 1);
			}
			else
			{
				return std::string(path);
			}
		}

		std::string get_basename(const char *path)
		{
			if (const char* sep = strrchr(path, get_path_separator()))
			{
				return get_path_without_extension(sep + 1);
			}
			else if (const char* sep = strrchr(path, get_alt_path_separator()))
			{
				return get_path_without_extension(sep + 1);
			}
			else
			{
				return get_path_without_extension(path);
			}
		}

		std::string join(const std::string& a, const std::string& b)
		{
			if (!a.empty() && *(a.end() - 1) != get_path_separator())
			{
				return a + get_path_separator() + b;
			}
			else
			{
				return a + b;
			}
		}

		std::string join(const string_vector &elements)
		{
			const char glue[2] = { get_path_separator(), 0 };
			return cbl::join(elements, glue);
		}

		const char *get_cppbuild_cache_path()
		{
			return "cppbuild-cache";
		}

		std::string get_relative_to(const char *path, const char *to_)
		{
			std::string to;
			if (to_)
				to = to_;
			else
				to = get_working_path();

			auto a_abs = get_absolute(path);
			auto b_abs = get_absolute(to.c_str());

			auto a = split(a_abs.c_str());
			auto b = split(b_abs.c_str());

#if defined(_WIN64)
			{
				const auto &a_fr = a.front();
				const auto &b_fr = b.front();
				// Paths on different drives can't be relative.
				if (a_fr.size() == 2 && b_fr.size() == 2
					&& a_fr[0] != b_fr[0	]
					&& a_fr[1] == ':' && b_fr[1] == ':')
					return a_abs;
			}
#endif

			// Find the furthest common root.
			while (!a.empty() && !b.empty())
			{
				if (a.front() == b.front())
				{
					a.erase(a.begin());
					b.erase(b.begin());
				}
			}

			// Go up the tree as far as needed.
			for (size_t i = b.size(); i > 0; --i)
			{
				a.insert(a.begin(), "..");
			}

			return join(a);
		}
	};

	string_vector vwrap(const std::string& s)
	{
		string_vector v;
		v.emplace_back(s);
		return v;
	}

	std::function<string_vector()> fvwrap(const std::string& s)
	{
		return [s]() { return vwrap(s); };
	}

	void trim(std::string& s)
	{
		if (!s.empty())
		{
			int l = 0, r = s.size() - 1;

			while (l < s.size() && std::isspace(s[l++]));
			while (r >= 0 && std::isspace(s[r--]));

			if (l > r)
			{
				s.clear();
			}
			else
			{
				l--;
				r++;
				int wi = 0;
				while (l <= r)
				{
					s[wi++] = s[l++];
				}
				s.erase(wi);
			}
		}
	}

	std::string join(const string_vector& v, const char *glue)
	{
		std::string result;
		for (const auto& s : v)
		{
			if (!result.empty())
				result += glue;
			result += s;
		}
		return result;
	}

	string_vector split(const char *str, char separator)
	{
		string_vector elements;
		size_t len = strlen(str);
		const char *begin = str;
		for (size_t i = 0; i < len; ++i)
		{
			if (str + i < begin)
			{
				continue;
			}
			if (str[i] == separator)
			{
				if (i == 0)
				{
					continue;
				}

				std::string new_element(begin, str + i - begin);
				if (!elements.empty() && elements.back().length() == 0)
				{
					elements.back() = std::move(new_element);
				}
				else
				{
					elements.push_back(new_element);
				}

				begin = str + i + 1;
			}
		}
		if (*begin)
		{
			elements.push_back(begin);
		}
		return elements;
	}

	namespace detail
	{
		FILE *log_file_stream;
		FILE *trace_file_stream;

		background_delete::worker::worker(const string_vector &list_)
			: enki::ITaskSet(list_.size(), 100)
			, list(list_)
		{}

		void background_delete::worker::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
		{
			MTR_SCOPE("rotate_logs", "background_delete");
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
				constexpr size_t max_old_log_files = 10;	// TODO: Put this in config.
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

			// Once we're done processing, delete ourselves.
			// FIXME: Can't delete ourselves because the scheduler makes an atomic write afterwards.
#if 0
			delete this;
#else
			// The next best thing is leaking just the string, but not its data.
			log_dir.clear();
			log_dir.shrink_to_fit();
#endif
		}

		void rotate(const char *log_dir, const char *ext)
		{
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

		void rotate_traces(bool append_to_current)
		{
			// Rotate the latest log file to a sortable, timestamped format.
			std::string log_dir = path::join(path::get_cppbuild_cache_path(), "log");
			std::string log = path::join(log_dir, "cppbuild.json");

			if (append_to_current)
			{
				// FIXME: Implement merging of JSONs.
				//trace_file_stream = fopen(log.c_str(), "ab");
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
			callbacks->waitStart = [](uint32_t thread_index) { MTR_BEGIN("task", "wait"); };
			callbacks->waitStop = [](uint32_t thread_index) { MTR_END("task", "wait"); };
		}

		void rotate_logs(bool append_to_current)
		{
			// Rotate the latest log file to a sortable, timestamped format.
			std::string log_dir = path::join(path::get_cppbuild_cache_path(), "log");
			std::string log = path::join(log_dir, "cppbuild.log");
			if (append_to_current)
			{
				// Exponential back-off until the parent process terminates.
				auto duration = std::chrono::microseconds(50);
				for (int i = 0; i < 10; ++i)
				{
					log_file_stream = fopen(log.c_str(), "a");
					if (log_file_stream)
					{
						break;
					}
					else
					{
						std::this_thread::sleep_for(duration);
						duration *= 2;
					}
				}
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

		static constexpr severity compiled_log_level = severity::
#if _DEBUG
			debug
#else
			verbose
#endif
			;

		// Logging implementation. Thread safe at the cost of a mutex lock around the actual buffer emission.
		template<severity severity>
		void log(const char *fmt, va_list va)
		{
			cbl::severity runtime_log_level = static_cast<cbl::severity>(g_options.log_level.val.as_int32);
			if (compiled_log_level <= severity && runtime_log_level <= severity)
			{
				std::ostringstream thread_id;
				thread_id << std::this_thread::get_id();

#if defined(_GNU_SOURCE)
				char *buffer = nullptr;
				vasprintf(&buffer, fmt, va);
#else
				int required = vsnprintf(nullptr, 0, fmt, va);
				if (required <= 0)
					return;	// Nothing to do here.

				++required;	// Allocate the null terminator.
				static thread_local char static_buf[1024];
				char *buffer = required <= sizeof(static_buf)
					? static_buf
					: new char[required];
				vsnprintf(buffer, required, fmt, va);
				buffer[required - 1] = 0;
#endif
				FILE *output_stream = nullptr;
				switch (severity)
				{
				case severity::warning:
				case severity::error:
					output_stream = stderr;
					break;
				default:
					output_stream = stdout;
					break;
				}
				static constexpr const char *severity_tags[] =
				{
					"[Debug]",
					"[Verbose]",
					"[Info]",
					"[Warning]",
					"[Error]"
				};
				int h, m, s, us;
				time::of_day(time::now(), nullptr, nullptr, nullptr, &h, &m, &s, &us);

				{
					// This is the only part that can't happen in parallel.
					static std::mutex mutex;
					std::lock_guard<std::mutex> _(mutex);

					auto emit = [&](FILE *stream)
					{
						fprintf(stream, "[%02d:%02d:%02d.%03d][Thread %s]%s ", h, m, s, us / 1000,
							thread_id.str().c_str(), severity_tags[(int)severity]);
						fputs(buffer, stream);
						fputc('\n', stream);
					};
					emit(output_stream);
					if (log_file_stream)
					{
						emit(log_file_stream);
					}
				}
#if defined(_GNU_SOURCE)
				if (buffer)
					free(buffer);
#else
				if (buffer != static_buf)
					delete[] buffer;
#endif
			}
		}
	}

	template <severity s>
	void log(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<s>( fmt, va);
		va_end(va);
	}

	void log_debug(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::debug>(fmt, va);
		va_end(va);
	}

	void log_verbose(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::verbose>(fmt, va);
		va_end(va);
	}

	void info(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::info>(fmt, va);
		va_end(va);
	}

	void warning(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::warning>(fmt, va);
		va_end(va);
	}

	void error(const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::error>(fmt, va);
		va_end(va);
	}

	namespace time
	{
		scoped_timer::scoped_timer(const char *in_label, severity severity)
			: start(now())
			, label(in_label)
			, s(severity)
		{
			static constexpr const char fmt[] = "[Start] %s";
			switch (severity)
			{
			case severity::error: error(fmt, label); return;
			case severity::warning: warning(fmt, label); return;
			case severity::info: info(fmt, label); return;
			case severity::verbose: log_verbose(fmt, label); return;
			case severity::debug: log_debug(fmt, label); return;
			default: assert(!"Unknown severity"); info(fmt, label); return;
			}
		}

		scoped_timer::~scoped_timer()
		{
			uint64_t duration = duration_usec(start, now());
			static constexpr const char fmt[] = "[End  ] %s: %3.4fs";
			switch (s)
			{
			case severity::error: error(fmt, label, float(duration) * 0.000001f); return;
			case severity::warning: warning(fmt, label, float(duration) * 0.000001f); return;
			case severity::info: info(fmt, label, float(duration) * 0.000001f); return;
			case severity::verbose: log_verbose(fmt, label, float(duration) * 0.000001f); return;
			case severity::debug: log_debug(fmt, label, float(duration) * 0.000001f); return;
			default: assert(!"Unknown severity"); info(fmt, label, float(duration) * 0.000001f); return;
			}
		}
	}

	enki::TaskScheduler scheduler;
};