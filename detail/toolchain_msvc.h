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

#pragma once

#if defined(_WIN64)

#include "../cppbuild.h"

struct msvc : public generic_cpp_toolchain
{
	constexpr static const char key[] = "msvc";

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

	cbl::deferred_process launch_cl_exe(const char *response, const char *additional_args);

private:

	std::string include_dirs[num_components];
	std::string lib_dirs[num_components];
	std::string compiler_dir;
	std::string cl_exe_path;

	std::string generate_system_include_directories();
	std::string generate_system_library_directories();

	std::string generate_cl_commandline_shared(build_context &, const bool for_linking);
};

#endif	// defined(_WIN64)
