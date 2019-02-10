#include "../cppbuild.h"

#include <cstdarg>
#include <cctype>
#include <mutex>
#include <thread>
#include <sstream>
#include "../cbl.h"
#include "detail.h"

namespace cbl
{
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
		detail::log<s>(fmt, va);
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
