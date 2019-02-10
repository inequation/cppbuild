#pragma once

#include "../cppbuild.h"

struct gcc : public toolchain
{
	constexpr static const char key[] = "gcc";

	static std::string get_object_for_cpptu(
		const std::string& source,
		const target &target,
		const configuration &cfg);

	virtual void pick_toolchain_versions();

	bool initialize() override;

	cbl::deferred_process schedule_compiler(
		const target& target,
		const std::string& object,
		const std::string& source,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override;

	cbl::deferred_process schedule_linker(
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

	bool deploy_executable_with_debug_symbols(
		const char *existing_path,
		const char *new_path) override;
	
	gcc();

protected:
	static version query_gcc_version(const char *path);

private:
	std::string compiler_dir;

	std::string generate_gcc_commandline_shared(const target& target, const configuration& cfg, const bool for_linking);
};
