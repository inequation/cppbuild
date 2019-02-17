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

	platform_count
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

	enum
	{
		Cxx98 = 98,
		Cxx03 = 03,
		Cxx11 = 11,
		Cxx14 = 14,
		Cxx17 = 17,
		Cxx20 = 20
	} standard;

	bool emit_debug_information;
	enum
	{
		O0, O1, O2, O3,
		Os = 100
	} optimize;
	bool use_debug_crt;
	bool use_exceptions;
	
	std::vector<std::pair<std::string, std::string>> definitions;
	// Transient definitions aren't part of the response file and their value does not affect whether the action is up to date. Use them for things such as revision/changelist information.
	std::vector<std::pair<std::string, std::string>> transient_definitions;
	string_vector additional_include_directories;

	std::unordered_map<std::string, std::string> additional_toolchain_options;
};
typedef std::pair<std::string, configuration_data> configuration;

namespace graph
{
	struct action;
	struct cpp_action;
	using action_vector = std::vector<std::shared_ptr<action>>;
};
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
	virtual cbl::deferred_process schedule_compiler(struct build_context &,
		const char *path_to_response_file) = 0;
	virtual cbl::deferred_process schedule_linker(struct build_context &,
		const char *path_to_response_file) = 0;
	virtual std::shared_ptr<graph::action> generate_compile_action_for_cpptu(
		struct build_context &,
		const char *path) = 0;
	virtual std::shared_ptr<graph::action> generate_link_action_for_objects(
		struct build_context &,
		const graph::action_vector& objects) = 0;
	virtual bool deploy_executable_with_debug_symbols(
		const char *existing_path,
		const char *new_path) = 0;
};

struct generic_cpp_toolchain : public toolchain
{
	std::shared_ptr<graph::action> generate_compile_action_for_cpptu(
		struct build_context &,
		const char *tu_path) override;

	std::shared_ptr<graph::action> generate_link_action_for_objects(
		struct build_context &,
		const graph::action_vector& objects) override;

	virtual void generate_dependency_actions_for_cpptu(
		struct build_context &,
		const char *source,
		const char *response_file,
		const char *response,
		std::vector<std::shared_ptr<graph::action>>& inputs) = 0;

	virtual std::string generate_compiler_response(
		struct build_context &,
		const char *object_path,
		const char *source_path) = 0;
	virtual std::string generate_linker_response(
		struct build_context &,
		const char *product_path,
		const graph::action_vector& source_paths) = 0;

	virtual std::string get_object_for_cpptu(
		struct build_context &,
		const char *source) = 0;
	static std::string get_intermediate_path_for_cpptu(
		struct build_context &,
		const char *source_path,
		const char *object_extension);
	static std::string get_response_file_for_cpptu(
		struct build_context &,
		const char *source_path);
	static std::string get_response_file_for_link_product(
		struct build_context &,
		const char *product_path);
	static void update_response_file(
		struct build_context &,
		const char *response_file,
		const char *response_str);
};

typedef std::unordered_map<std::string, configuration_data> configuration_map;
typedef std::unordered_map<std::string, std::shared_ptr<toolchain>> toolchain_map;
struct build_context
{
	const target &trg;
	const configuration &cfg;
	toolchain &tc;
};

//=============================================================================

namespace graph
{
	struct action
	{
		enum action_type
		{
			cpp_actions_begin = 0,							// Beginning of C++-specific action types. For details, see cpp_action.
			cpp_actions_end = /*cpp_action::include*/3 + 1,	// End of C++-specific action types.

			deploy_actions_begin = cpp_actions_end,			// Beginning of deployment action types.
			deploy_actions_end = deploy_actions_begin + 1,				// End of deployment action types.

			custom_actions_begin = deploy_actions_end		// Beginning of custom, user-implementable actions.
		} type;
		action_vector inputs;
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
		};
		static_assert((action::action_type)include < action::cpp_actions_end, "Action type range overflow; increase action::cpp_actions_end");

		std::string response_file;

		bool are_dependencies_met() override;
	};

	using dependency_timestamp_vector = std::vector<std::pair<std::string, uint64_t>>;
	bool query_dependency_cache(build_context &,
		const std::string& source,
		const char *response,
		std::function<void(const std::string &)> push_dep);
	void insert_dependency_cache(build_context &,
		const std::string& source,
		const char *response,
		const dependency_timestamp_vector &deps);
	void save_timestamp_caches();

	std::shared_ptr<action> generate_cpp_build_graph(build_context &);
	void cull_build_graph(build_context &,
		std::shared_ptr<action>& root);
	int execute_build_graph(build_context &,
		std::shared_ptr<action> root);
	void clean_build_graph_outputs(build_context &,
		std::shared_ptr<action> root);
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
