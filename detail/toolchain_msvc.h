#pragma once

#if defined(_WIN64)

#include "../cppbuild.h"

struct msvc : public toolchain
{
	constexpr static const char key[] = "msvc";

	static std::string get_object_for_cpptu(const std::string& source, const target &target, const configuration &cfg);

	virtual void pick_toolchain_versions();

	bool initialize() override;

	cbl::deferred_process invoke_compiler(
		const target& target,
		const std::string& object,
		const std::string& source,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override;

	cbl::deferred_process invoke_linker(
		const target& target,
		const string_vector& source_paths,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override;

	void generate_dependency_actions_for_cpptu(
		const target& target,
		const std::string& source,
		const configuration& cfg,
		std::vector<std::shared_ptr<graph::action>>& inputs);
	
	std::shared_ptr<graph::action> generate_compile_action_for_cpptu(
		const target& target,
		const std::string& tu_path,
		const configuration& cfg) override;

	bool deploy_executable_with_debug_symbols(const char *existing_path, const char *new_path) override;
	
	msvc();

protected:
	enum { component_sdk, component_ucrt, component_compiler, num_components };
	using discovered_components = std::unordered_map<version, std::string[2]>;

	static void discover_windows_sdks(
		discovered_components& sdk_dirs,
		discovered_components& crt_dirs
	);

	static version query_cl_exe_version(const char *path);
	static void discover_compilers(discovered_components& compiler_dirs);

private:

	std::string include_dirs[num_components];
	std::string lib_dirs[num_components];
	std::string compiler_dir;

	std::string generate_system_include_directories();
	std::string generate_system_library_directories();

	std::string generate_cl_commandline_shared(const target& target, const configuration& cfg, const bool for_linking);
};

#endif	// defined(_WIN64)
