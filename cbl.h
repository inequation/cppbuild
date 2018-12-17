#pragma once

typedef std::vector<std::string> string_vector;

namespace cbl
{
	namespace path
	{
		constexpr const char get_path_separator();
		constexpr const char get_alt_path_separator();
		bool is_path_separator(char c);

		std::string get_extension(const char *path);
		std::string get_path_without_extension(const char *path);
		std::string get_directory(const char *path);
		std::string get_filename(const char *path);
		std::string get_basename(const char *path);
		std::string get_absolute(const char *path);
		// If to is nullptr, current working directory is used.
		std::string get_relative_to(const char *path, const char *to = nullptr);

		// Splits a path along path separators.
		string_vector split(const char *path);
		// Joins paths using the host platform-specific path separator.
		std::string join(const std::string& a, const std::string& b);
		template<typename... Args>
		std::string join(const std::string& a, const std::string& b, Args const&... args)
		{
			return join(join(a, b), args...);
		}


		// Returns the current working directory.
		std::string get_working_path();
		const char *get_cppbuild_cache_path();
	};

	namespace fs
	{
		string_vector enumerate_files(const char *path);
		string_vector enumerate_directories(const char *path);

		uint64_t get_modification_timestamp(const char *path);

		bool mkdir(const char *path, bool make_parent_directories);

		enum copy_flags
		{
			overwrite = 0x1,
			maintain_timestamps = 0x2
		};
		copy_flags operator|(copy_flags a, copy_flags b) { return (copy_flags)((int)a | (int)b); }
		bool copy_file(const char *existing_path, const char *new_path, copy_flags flags);
		bool move_file(const char *existing_path, const char *new_path, copy_flags flags);
		bool delete_file(const char *path);
	};

	// Factories for generating typical basic configurations.
	namespace base_configurations
	{
		static const configuration_data debug(platform p)
		{
			static configuration_data c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration_data::O1;
			c.use_debug_crt = true;
			return c;
		};

		static const configuration_data release(platform p)
		{
			static configuration_data c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration_data::O2;
			c.use_debug_crt = true;
			return c;
		};

		static const configuration_data shipping(platform p)
		{
			static configuration_data c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration_data::O3;
			c.use_debug_crt = false;
			return c;
		};
	};

	struct process
	{
		pipe_output_callback on_out, on_err;
		void *handle;
		void *in[2];
		void *out[2];
		void *err[2];

	private:
		process();
		enum { pipe_read, pipe_write };
	public:
		static std::shared_ptr<process> start_async(const char *commandline,
			pipe_output_callback on_stderr = nullptr,
			pipe_output_callback on_stdout = nullptr,
			const std::vector<uint8_t> *stdin_buffer = nullptr, void *environment = nullptr);

		// Explicitly gives up any control over the process and lets it run in the background.
		void detach();

		// Waits for the process to finish. Returns its exit code.
		int wait();

		// Waits for all of the processes in the given group to finish. Returns a vector of their exit codes.
		static std::vector<int> wait_for_multiple(const std::vector<std::shared_ptr<process>>& processes);

		static uint32_t get_current_pid();

		static std::string get_current_executable_path();

		static void wait_for_pid(uint32_t pid);
	};

	constexpr platform get_host_platform();
	constexpr const char *get_platform_str(platform);
	constexpr const char *get_host_platform_str();

	constexpr const char *get_default_toolchain_for_host();

	size_t combine_hash(size_t a, size_t b);

	enum class severity : uint8_t
	{
		verbose,
		info,
		warning,
		error
	};
	template<severity>
	void log(const char *fmt, ...);
	// Alias for log<info>(...).
	void info(const char* fmt, ...);
	// Alias for log<warning>(...).
	void warning(const char* fmt, ...);
	// Alias for log<error>(...).
	void error(const char* fmt, ...);
	// Alias for log<verbose>(...).
	void log_verbose(const char* fmt, ...);

	// Wraps a single string in a vector.
	string_vector vwrap(const std::string& s);
	// Wraps a string vector in a callable functor.
	std::function<string_vector()> fvwrap(const std::string& s);

	// Removes whitespace (any character for which std::isspace() returns true) at both ends of the string.
	void trim(std::string& s);

	// Concatenates the string using the specified glue string.
	std::string join(const string_vector& v, const char *glue);

	namespace time
	{
		// Returns an opaque, filesystem-specific, platform-specific timestamp for the current point in time.
		uint64_t now();

		// Breaks down an opaque, 64-bit stamp into year/month/day (indexed from 1) and hours/minutes/seconds/microseconds (indexed from 0) in the local time zone.
		void of_day(const uint64_t stamp, int *year, int *month, int *day, int *hour, int *minute, int *second, int *us);

		// Converts a difference of opaque, 64-bit stamps, in microseconds.
		uint64_t duration_usec(uint64_t begin, uint64_t end);

		// Scoped timer (RAII-style) that outputs the time elapsed from construction to destruction in the format of "%s: %3.4fs".
		struct scoped_timer
		{
			scoped_timer(const char *label, severity severity = severity::info);
			~scoped_timer();
		private:
			uint64_t start;
			const char *label;
			severity s;
		};
	}
}

#if _WIN64
	#include "cbl_win64.h"
#endif
