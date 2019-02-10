#pragma once

#if __cplusplus < 201103L && (!defined(_MSC_VER) || _MSC_VER < 1900)
	#error C++11 is required by cppbuild
#endif

#ifndef CPPBUILD_GENERATION
	#define CPPBUILD_GENERATION	0
#endif

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
#include <cstring>
#include <cinttypes>

typedef std::vector<std::string> string_vector;

enum class platform
{
	win64,
	linux64,
	macos,
	ps4,
	xbox1,
};

struct version
{
	uint16_t major, minor, build, revision;
	char tag[64];

	bool operator==(const version& other) const;
	bool operator<(const version& other) const;
	bool parse(const char *str);
	std::string to_string() const;
};

extern const version cppbuild_version;

struct configuration_data
{
	::platform platform;

	bool emit_debug_information;
	enum
	{
		O0, O1, O2, O3,
		Os = 100
	} optimize;
	bool use_debug_crt;
	
	std::vector<std::pair<std::string, std::string>> definitions;
	string_vector additional_include_directories;

	bool use_incremental_linking;

	std::unordered_map<std::string, string_vector> additional_toolchain_options;
};
typedef std::pair<std::string, configuration_data> configuration;

namespace graph { struct action; };
namespace cbl
{
	struct process;
	class deferred_process;
	typedef std::function<void(const void*, size_t)> pipe_output_callback;
};

struct target_data
{
	enum target_type
	{
		executable,
		static_library,
		dynamic_library
	} type;

	const char *used_toolchain = nullptr;
	std::string output;
	std::function<string_vector()> sources;

	target_data() = default;

	target_data(target_type in_type, const char *in_output, std::function<string_vector()> in_sources, const char *in_toolchain = nullptr)
		: type(in_type)
		, output(in_output)
		, used_toolchain(in_toolchain)
		, sources(in_sources)
	{}
};
typedef std::unordered_map<std::string, target_data> target_map;
typedef std::pair<std::string, target_data> target;

//=============================================================================

struct toolchain
{
	virtual bool initialize() = 0;
	virtual cbl::deferred_process schedule_compiler(
		const target& target,
		const std::string& object_path,
		const std::string& source_path,
		const configuration&,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) = 0;
	virtual cbl::deferred_process schedule_linker(
		const target& target,
		const string_vector& source_paths,
		const configuration&,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) = 0;
	virtual std::shared_ptr<graph::action> generate_compile_action_for_cpptu(
		const target& target,
		const std::string& path,
		const configuration&)
	{ return nullptr; };
	virtual bool deploy_executable_with_debug_symbols(const char *existing_path, const char *new_path) = 0;
	static std::string get_intermediate_path_for_cpptu(const char *source_path, const char *object_extension, const target &, const configuration &);
};

typedef std::unordered_map<std::string, configuration_data> configuration_map;
typedef std::unordered_map<std::string, std::shared_ptr<toolchain>> toolchain_map;

//=============================================================================

namespace graph
{
	struct action
	{
		enum action_type
		{
			cpp_actions_begin = 0,						// Beginning of C++-specific action types. For details, see cpp_action.
			cpp_actions_end = 5,						// End of C++-specific action types.
			deploy_actions_begin = cpp_actions_end,		// Beginning of deployment action types.
			deploy_actions_end,							// End of deployment action types.
			custom_actions_begin = deploy_actions_end	// Beginning of custom, user-implementable actions.
		} type;
		std::vector<std::shared_ptr<action>> inputs;
		string_vector outputs;
		std::vector<uint64_t> output_timestamps;

		virtual bool are_dependencies_met() = 0;
		virtual uint64_t get_oldest_output_timestamp();
		void update_output_timestamps();
	};

	struct cpp_action : public action
	{
		enum
		{
			link = action::cpp_actions_begin,	// Linking (or archiving, in case of a static library) an executable or a library.
			compile,							// Translation unit compilation.
			source,								// Symbollic action of a translation unit. May only have include inputs.
			include,							// Included file that does not get directly compiled as a TU, but influences the output. Cannot have further inputs, nested includes are listed as inputs to the TU that includes them.
			generate							// Generated source, e.g. by means of MOC in Qt-based projects. May have inputs (i.e. source files for the generator); generated files with no inputs will be treated as always out of date.
		};
		static_assert((action::action_type)generate < action::cpp_actions_end, "Action type range overflow; increase action::cpp_actions_end");

		bool are_dependencies_met() override;
	};

	using dependency_timestamp_vector = std::vector<std::pair<std::string, uint64_t>>;
	using timestamp_cache = std::unordered_map<std::string, dependency_timestamp_vector>;
	using timestamp_cache_entry = timestamp_cache::value_type;
	bool query_dependency_cache(const target &target, const configuration &cfg, const std::string& source, std::function<void(const std::string &)> push_dep);
	void insert_dependency_cache(const target &target, const configuration &cfg, const std::string& source, const dependency_timestamp_vector &deps);
	void save_timestamp_caches();

	std::shared_ptr<action> generate_cpp_build_graph(const target& target, const configuration& c, std::shared_ptr<struct toolchain> tc);
	void cull_build_graph(std::shared_ptr<action>& root);
	int execute_build_graph(
		const target& target,
		std::shared_ptr<action> root,
		const configuration& cfg,
		std::shared_ptr<toolchain> tc,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout);
	void clean_build_graph_outputs(std::shared_ptr<action> root);
};

//=============================================================================

// Parses the given argument string vector as command line options. allows overriding defaults from describe().
// argv[0] is also parsed as an option. Non-option arguments (i.e. not starting with '-', and not arguments
// to such options) are ignored.
extern void override_options(const string_vector &args);

//=============================================================================

// User implementables
std::pair<std::string, std::string> describe(target_map& targets, configuration_map& configs, toolchain_map& toolchains);

//=============================================================================

// Internal implementations
#if CPPBUILD_GENERATION < 2
	#include "detail/core.cpp"
	#include "detail/cbl.cpp"
	#include "detail/cbl_win64.cpp"
	#include "detail/cbl_linux.cpp"
	#include "detail/graph.cpp"
	#include "detail/toolchain.cpp"
	#include "detail/toolchain_msvc.cpp"
	#include "detail/toolchain_gcc.cpp"
	#include "detail/main.cpp"
	#include "detail/enkiTS/src/TaskScheduler.cpp"
	extern "C"
	{
		#include "detail/minitrace/minitrace.c"
	}
	#if !defined(_GNU_SOURCE) && !defined(_BSD_SOURCE)
		#include "detail/getopt/getopt.c"
		#include "detail/getopt/getopt_long.c"
	#endif
#endif
