#pragma once

namespace cbl
{
	namespace path
	{
		constexpr const char get_path_separator();
		bool is_path_separator(char c);

		std::string get_extension(const char *path);
		std::string get_path_without_extension(const char *path);

		// Splits a path along path separators.
		string_vector split(const char *path);
		// Joins paths using the host platform-specific path separator.
		std::string join(const std::string& a, const std::string& b);
	};

	namespace fs
	{
		string_vector enumerate_files(const char *path);

		uint64_t get_modification_timestamp(const char *path);

		bool mkdir(const char *path, bool make_parent_directories);

		enum copy_flags
		{
			overwrite = 0x1,
			maintain_timestamps = 0x2
		};
		copy_flags operator|(copy_flags a, copy_flags b) { return (copy_flags)((int)a | (int)b); }
		bool copy_file(const char *existing_path, const char *new_path, copy_flags flags);
	};

	// Factories for generating typical basic configurations.
	namespace base_configurations
	{
		static const configuration debug(platform p)
		{
			static configuration c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration::O1;
			c.definitions.push_back(std::make_pair("_DEBUG", ""));
			return c;
		};

		static const configuration release(platform p)
		{
			static configuration c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration::O2;
			c.definitions.push_back(std::make_pair("NDEBUG", "1"));
			return c;
		};

		static const configuration shipping(platform p)
		{
			static configuration c;
			c.platform = p;
			c.emit_debug_information = true;
			c.optimize = configuration::O3;
			c.definitions.push_back(std::make_pair("NDEBUG", "2"));
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

	constexpr const char* get_default_toolchain_for_host();

	// Wraps a single string in a vector.
	string_vector vwrap(const std::string& s);
	// Wraps a string vector in a callable functor.
	std::function<string_vector()> fvwrap(const std::string& s);

	// Removes whitespace (any character for which std::isspace() returns true) at both ends of the string.
	void trim(std::string& s);

	// Concatenates the string using the specified glue string.
	std::string join(const string_vector& v, const char *glue);

	// Returns a filesystem-specific, platform-specific timestamp for the current point in time.
	uint64_t now();
}
