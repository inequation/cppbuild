#pragma once

#include "../cppbuild.h"

struct gcc : public generic_cpp_toolchain
{
	constexpr static const char key[] = "gcc";

	virtual void pick_toolchain_versions();

	bool initialize() override;

	std::string generate_compiler_response(
		build_context &,
		const char *object,
		const char *source) override;

	std::string generate_linker_response(
		build_context &,
		const char *product_path,
		const graph::action_vector& source_paths) override;

	cbl::deferred_process schedule_compiler(build_context &,
		const char *path_to_response_file) override;
	cbl::deferred_process schedule_linker(build_context &,
		const char *path_to_response_file) override;

	void generate_dependency_actions_for_cpptu(
		build_context &,
		const char *source,
		const char *response_file,
		const char *response,
		std::vector<std::shared_ptr<graph::action>>& inputs) override;

	std::string get_object_for_cpptu(
		build_context &,
		const char *source) override;

	bool deploy_executable_with_debug_symbols(
		const char *existing_path,
		const char *new_path) override;
	
	gcc();

protected:
	static version query_gcc_version(const char *path);

	cbl::deferred_process launch_gcc(const char *response, const char *additional_args);

private:
	std::string gcc_path;

	std::string generate_gcc_commandline_shared(build_context &, const bool for_linking);
};
