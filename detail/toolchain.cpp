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
#include "../cbl.h"
#include "detail.h"
#include "toolchain_msvc.h"
#include "toolchain_gcc.h"

void discover_toolchains(toolchain_map& toolchains)
{
	MTR_SCOPE_FUNC();
	std::shared_ptr<toolchain> tc;
	// FIXME: Use parallel_for.
#if defined(_WIN64)
	tc = std::make_shared<msvc>();
	if (tc->initialize())
		toolchains[msvc::key] = tc;
#endif
	tc = std::make_shared<gcc>();
	if (tc->initialize())
		toolchains[gcc::key] = tc;
	if (toolchains.empty())
	{
		cbl::error("No toolchains discovered. Check verbose log for details.");
		abort();
	}
}

std::string generic_cpp_toolchain::get_intermediate_path_for_cpptu(build_context &ctx, const char *source_path, const char *object_extension)
{
	using namespace cbl::path;
	return join(
		get_cppbuild_cache_path(),
		cbl::get_platform_str(ctx.cfg.second.platform),
		ctx.cfg.first,
		ctx.trg.first,
		get_path_without_extension(
			get_relative_to(source_path).c_str()) + object_extension);
}

std::string generic_cpp_toolchain::get_response_file_for_cpptu(build_context &ctx, const char *source_path)
{
	return get_intermediate_path_for_cpptu(ctx, source_path, ".response");
}

std::string generic_cpp_toolchain::get_response_file_for_link_product(build_context &ctx, const char *product_path)
{
	return get_intermediate_path_for_cpptu(ctx, product_path, ".response");
}

static inline void internal_write_response_file(const char *path, const std::string &response)
{
	using namespace cbl;
	fs::mkdir(path::get_directory(path).c_str(), true);
	if (FILE *f = fopen(path, "wb"))
	{
		fwrite(response.data(), 1, response.size(), f);
		fclose(f);
	}
	else
	{
		cbl::fatal((int)error_code::failed_writing_response_file, "Failed to write response file, reason: %s", strerror(errno));
	}
}

void generic_cpp_toolchain::update_response_file(build_context &ctx, const char *response_file, const char *response_str)
{
	FILE *f = fopen(response_file, "rb");
	size_t current_response_len = strlen(response_str);
	
	if (f)
	{
		std::string old_response;
		old_response.resize(current_response_len);
		size_t bytes = 0;
		for (;;)
		{
			bytes += fread(const_cast<char *>(old_response.data()) + bytes, 1, old_response.size() - bytes, f);
			if (feof(f))
				break;
			old_response.resize(old_response.size() * 2);
		}
		fclose(f);
		old_response.push_back(0);
		old_response.resize(strlen(old_response.c_str()));
		if (old_response == response_str)
			return;
	}
	
	internal_write_response_file(response_file, response_str);
}

std::shared_ptr<graph::action> generic_cpp_toolchain::generate_compile_action_for_cpptu(
	build_context &ctx,
	const char *tu_path)
{
	auto source = std::make_shared<graph::cpp_action>();
	auto action = std::make_shared<graph::cpp_action>();
	auto object = get_object_for_cpptu(ctx, tu_path);

	action->type = (graph::action::action_type)graph::cpp_action::compile;
	action->outputs.push_back(object);
	action->inputs.push_back(source);
	action->response_file = get_response_file_for_cpptu(ctx, tu_path);

	auto response = generate_compiler_response(ctx, object.c_str(), tu_path);
	update_response_file(ctx, action->response_file.c_str(), response.c_str());

	source->type = (graph::action::action_type)graph::cpp_action::source;
	source->outputs.push_back(tu_path);
	generate_dependency_actions_for_cpptu(ctx, tu_path, action->response_file.c_str(), response.c_str(), source->inputs);

	return action;
}

std::shared_ptr<graph::action> generic_cpp_toolchain::generate_link_action_for_objects(
	build_context &ctx,
	const graph::action_vector& objects)
{
	using namespace graph;

	auto link = std::make_shared<cpp_action>();
	link->type = (action::action_type)cpp_action::link;
	link->outputs.push_back(ctx.trg.second.output);
	link->inputs.insert(link->inputs.begin(), objects.begin(), objects.end());
	link->response_file = get_response_file_for_link_product(ctx, ctx.trg.second.output.c_str());

	auto response = generate_linker_response(ctx, ctx.trg.second.output.c_str(), objects);
	update_response_file(ctx, link->response_file.c_str(), response.c_str());

	return link;
}
