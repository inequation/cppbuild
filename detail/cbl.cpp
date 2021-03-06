/*
MIT License

Copyright (c) 2019 Leszek Godlewski

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

		std::string get_normalised(const char *path)
		{
			std::string norm(path);
			for (char &c : norm) { if (c == '/' || c == '\\') c = get_path_separator(); }
			return norm;
		}
	};

	namespace fs
	{
		cache_update_result update_file_backed_cache(const char *path, const void *contents, size_t byte_count)
		{
			// TODO: Hashing instead of memcmp().
			using namespace cbl;

			if (FILE * f = fopen(path, "rb"))
			{
				std::vector<uint8_t> existing_cache;
				existing_cache.resize(byte_count);	// Hint for expected size.
				size_t bytes = 0;
				for (;;)
				{
					bytes += fread(const_cast<uint8_t *>(existing_cache.data()) + bytes, 1, existing_cache.size() - bytes, f);
					if (feof(f) || ferror(f))
						break;
					existing_cache.resize(existing_cache.size() * 2);
				}
				// Assume outdated if there was an error.
				bool had_error = ferror(f);
				fclose(f);
				if (!had_error && bytes > 0 && 0 == memcmp(existing_cache.data(), contents, bytes))
					return cache_update_result::up_to_date;
			}

			// If we get here, the file was deemed outdated.
			fs::mkdir(path::get_directory(path).c_str(), true);
			if (FILE * f = fopen(path, "wb"))
			{
				size_t bytes = 0;
				for (;;)
				{
					bytes += fwrite(static_cast<const uint8_t *>(contents) + bytes, 1, byte_count - bytes, f);
					if (ferror(f) || bytes == byte_count)
						break;
				}
				bool had_error = ferror(f);
				fclose(f);
				if (!had_error)
					return cache_update_result::outdated_success;
			}
			cbl::warning("Failed to write file-backed cache %s, reason: %s", path, strerror(errno));
			return cache_update_result::outdated_failure;
		}
	}
	
	std::function<string_vector()> fvwrap(const std::string& s)
	{
		return [s]() { return string_vector{ s }; };
	}

	void trim(std::string& s)
	{
		if (!s.empty())
		{
			int l = 0, r = s.size() - 1;

			auto is_space_or_zero = [](int c) { return c == 0 || std::isspace(c); };
			while (l < s.size() && std::isspace(s[l++]));
			while (r >= 0 && is_space_or_zero(s[r--]));

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

	std::string& jsonify(std::string& s)
	{
		for (auto& c : s) { if (c == '\\') c = '/'; }
		return s;
	}

	std::string jsonify(const std::string& s)
	{
		return jsonify(std::string(s));
	}

	std::string&& jsonify(std::string&& s)
	{
		for (auto& c : s) { if (c == '\\') c = '/'; }
		return std::move(s);
	}

	std::string jsonify(char *s)
	{
		return jsonify(std::string(s));
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

		void internal_log(severity severity, const char *buffer)
		{
			std::ostringstream thread_id;
			thread_id << std::this_thread::get_id();

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
#if defined(_WIN64)
				// This doesn't decorate the output with timestamp and thread ID, but it's good enough for debug.
				OutputDebugStringA(severity_tags[(int)severity]);
				OutputDebugStringA(buffer);
				OutputDebugStringA("\n");
#endif
			}
		}

		// Logging implementation. Thread safe at the cost of a mutex lock around the actual buffer emission.
		template<severity severity>
		void log(const char *fmt, va_list va)
		{
			cbl::severity runtime_log_level = static_cast<cbl::severity>(g_options.log_level.val.as_int32);
			if (compiled_log_level <= severity && runtime_log_level <= severity)
			{
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
				internal_log(severity, buffer);
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

	void fatal(int exit_code, const char *fmt, ...)
	{
		va_list va;
		va_start(va, fmt);
		detail::log<severity::error>(fmt, va);
		va_end(va);
		// Make sure to flush all logs.
		if (detail::log_file_stream)
			fclose(detail::log_file_stream);
		if (detail::trace_file_stream)
			mtr_shutdown();
		// Just terminate without cleanup.
		terminate_process_group(exit_code);
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
