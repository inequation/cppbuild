#pragma once

#if __cplusplus < 201103L && (!defined(_MSC_VER) || _MSC_VER < 1900)
	#error C++11 is required by cppbuild
#endif

enum class platform
{
	win64,
	linux,
	macos,
	ps4,
	xbox1,
};

#include "detail/core.inl"

struct configuration
{
	platform platform;

	bool emit_debug_information;
	enum
	{
		O0, O1, O2, O3,
		Os = 100
	} optimize;

	std::vector<std::pair<std::string, std::string>> definitions;
	string_vector additional_include_directories;

	std::unordered_map<std::string, string_vector> additional_toolchain_options;
};

namespace graph { struct action; };
namespace cbl
{
	struct process;
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
	std::function<string_vector()> sources;

	target_data() = default;

	target_data(target_type in_type, std::function<string_vector()> in_sources, const char *in_toolchain = nullptr)
		: type(in_type)
		, used_toolchain(in_toolchain)
		, sources(in_sources)
	{}
};
typedef std::unordered_map<std::string, target_data> target_map;
typedef std::pair<std::string, target_data> target;

struct toolchain
{
	virtual void initialize(const configuration&) = 0;
	virtual std::shared_ptr<cbl::process> invoke_compiler(
		const std::string& object_path,
		const std::string& source_path,
		const configuration&,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) = 0;
	virtual std::shared_ptr<cbl::process> invoke_linker(
		const target& target,
		const string_vector& source_paths,
		const configuration&,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) = 0;
	virtual std::shared_ptr<graph::action> generate_compile_action_for_cpptu(const std::string& path, const configuration&) { return nullptr; };
};

int build_target(target&, configuration&);

typedef std::unordered_map<std::string, configuration> configuration_map;
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
		static_assert(generate < action::cpp_actions_end, "Action type range overflow; increase action::cpp_actions_end");

		bool are_dependencies_met() override;
	};

	std::shared_ptr<action> generate_cpp_build_graph(const target& target, const configuration& c, std::shared_ptr<struct toolchain> tc);
	void cull_build_graph(std::shared_ptr<action>& root);
	int execute_build_graph(
		const target& target,
		std::shared_ptr<action> root,
		const configuration& cfg,
		std::shared_ptr<toolchain> tc,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout);
};

void discover_toolchains(toolchain_map& toolchains);

//=============================================================================

// User implementables
void describe(target_map& targets, configuration_map& configs, toolchain_map& toolchains);

//=============================================================================

#include "detail/cbl.inl"
#include "detail/graph.inl"
#include "detail/toolchain.inl"
#include "detail/main.inl"
