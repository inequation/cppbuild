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

#include "toolchain_gcc.h"
#include "../cbl.h"

// Storage.
constexpr const char gcc::key[];

std::string gcc::get_object_for_cpptu(
	build_context &ctx,
	const char *source)
{
	return get_intermediate_path_for_cpptu(ctx, source, ".o");
}

void gcc::pick_toolchain_versions()
{
	// FIXME: This is completely unportable.
	auto version = query_gcc_version("/usr/bin/g++");
	gcc_path = "\"/usr/bin/g++\"";
}

bool gcc::initialize()
{
	pick_toolchain_versions();
		
	if (gcc_path.empty())
	{
		cbl::log_verbose("GCC binary not found.");
		return false;
	}
	return true;
}

std::string gcc::generate_compiler_response(
	build_context &ctx,
	const char *object,
	const char *source)
{
	std::string cmdline = generate_gcc_commandline_shared(ctx, false);
	cmdline += " -c -o ";
	cmdline += object;
	cmdline += " ";
	cmdline += source;
	cmdline += " -std=c++" + std::to_string(int(ctx.cfg.second.standard));
	return cmdline;
}

std::string gcc::generate_linker_response(
	build_context &ctx,
	const char *product_path,
	const graph::action_vector& objects)
{
	const char *addtn_opts = nullptr;
	{
		auto it = ctx.cfg.second.additional_toolchain_options.find("gcc link");
		if (it != ctx.cfg.second.additional_toolchain_options.end())
			addtn_opts = it->second.c_str();
	}
	switch (ctx.trg.second.type)
	{
	case target_data::executable:
	case target_data::dynamic_library:
		{
			std::string cmdline = generate_gcc_commandline_shared(ctx, true);
			cmdline += " -o ";
			cmdline += product_path;
			if (ctx.cfg.second.use_debug_crt)
			{
				// FIXME: mcheck is not thread-safe without additional synchronisation.
				//cmdline += " -lmcheck";
			}
			if (addtn_opts)
			{
				cmdline += addtn_opts;
			}
			for (auto &action : objects)
			{
				for (auto &output : action->outputs)
				{
					cmdline += ' ';
					cmdline += output;
				}
			}
			return cmdline;
		}
	default:
		assert(!"Unimplmented");
		// Call lib.exe for static libraries here.
		return "";
	}
}

void gcc::generate_dependency_actions_for_cpptu(
	build_context &ctx,
	const char *source,
	const char *response_file,
	const char *response,
	std::vector<std::shared_ptr<graph::action>>& inputs)
{
	auto push_dep = [&inputs](const std::string &name)
	{
		auto dep_action = std::make_shared<graph::cpp_action>();
		dep_action->type = (graph::action::action_type)graph::cpp_action::include;
		dep_action->outputs.push_back(name);
		inputs.push_back(dep_action);
	};

	if (graph::query_dependency_cache(ctx, source, response, push_dep))
		return;

	std::string transient_defines;
	for (auto& define : ctx.cfg.second.transient_definitions)
	{
		transient_defines += " -D" + define.first;
		if (!define.second.empty())
		{
			transient_defines += "=" + define.second;
		}
	}

	std::string cmdline = gcc_path;
	cmdline += transient_defines;
	cmdline += " -c -M @";
	cmdline += response_file;

	std::vector<uint8_t> buffer;
	auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
	{
		buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
	};

	std::string safe_source = cbl::jsonify(source);
	MTR_SCOPE_S(__FILE__, "Dependency scan", "source", safe_source.c_str());
	int exit_code = cbl::process::start_sync(cmdline.c_str(), append_to_buffer, append_to_buffer);
	if (exit_code == 0)
	{
		buffer.push_back(0);	// Ensure null termination, so that we may treat data() as C string.
		const char *s = strchr((char *)buffer.data(), ':');
		if (s)
		{
			s += 1;
			// Skip leading whitespace.
			while (*s && isspace(*s))
				++s;
			// Skip our source file.
			if (0 == strncmp(s, source, safe_source.size()))
				s += safe_source.size() + 1;

			const char *end = (const char *)&buffer.back();
			while (s < end)
			{
				const char *it = s;
				// Skip leading whitespace.
				while (*it && isspace(*it))
					++it;
				// Mark start of token.
				s = it;
				// Find end of token.
				while (*it && !isspace(*it))
					++it;
				// Ignore line breaks.
				if (it - s != 1 || *s != '\\')
				{
					std::string dep_name(s, it - s);
					push_dep(dep_name);
				}
				s = it + 1;
			}
		}

		graph::dependency_timestamp_vector deps;
		for (const auto& i : inputs)
		{
			if (i->output_timestamps.empty())
			{
				i->update_output_timestamps();
			}
			deps.push_back(std::make_pair(i->outputs[0], i->output_timestamps[0]));
		}
		graph::insert_dependency_cache(ctx, source, response, deps);
	}
	else
		cbl::fatal(exit_code, "%s: Dependency scan failed with code %d%s%s", source, exit_code,
			buffer.empty() ? "" : ", message:\n", buffer.empty() ? "" : (const char *)buffer.data());
}

cbl::deferred_process gcc::launch_gcc(const char *response, const char *additional_args)
{
	std::string cmdline = gcc_path;
	cmdline += " @";
	cmdline += response;
	if (additional_args)
		cmdline += additional_args;
	return cbl::process::start_deferred(cmdline.c_str());
}

cbl::deferred_process gcc::schedule_compiler(build_context &ctx, const char *response)
{
	std::string transient_defines;
	for (auto& define : ctx.cfg.second.transient_definitions)
	{
		transient_defines += " -D" + define.first;
		if (!define.second.empty())
		{
			transient_defines += "=" + define.second;
		}
	}
	return launch_gcc(response, transient_defines.c_str());
}

cbl::deferred_process gcc::schedule_linker(build_context &ctx, const char *response)
{
	return launch_gcc(response, nullptr);
}

bool gcc::deploy_executable_with_debug_symbols(
	const char *existing_path,
	const char *new_path)
{
	using namespace cbl;
	return
		fs::copy_file(
			existing_path,
			new_path,
			fs::overwrite | fs::maintain_timestamps);
}
	
gcc::gcc()
{}

version gcc::query_gcc_version(const char *path)
{
	version v{ 0, 0, 0, 0, "" };
	if (0 != cbl::fs::get_modification_timestamp(path))
	{
		static constexpr const char header[] = "gcc version ";

		std::string buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (const char *)data, ((const char *)data) + byte_count);
		};

		memset(&v, 0, sizeof(v));
		if (0 == cbl::process::start_sync((std::string(path) + " -v").c_str(), append_to_buffer, append_to_buffer))
		{
			if (const char *vstr = strstr(buffer.c_str(), header))
				v.parse(vstr + sizeof(header) - 1);
		}
	}
	return v;
}

std::string gcc::generate_gcc_commandline_shared(
	build_context &ctx,
	const bool for_linking)
{
	std::string cmdline;
	if (ctx.cfg.second.emit_debug_information)
	{
		cmdline += " -g";
	}
	if (ctx.cfg.second.optimize <= configuration_data::O3)
	{
		cmdline += " -O";
		cmdline += "0123"[ctx.cfg.second.optimize];
	}
	else if (ctx.cfg.second.optimize == configuration_data::Os)
	{
		cmdline += " -Os";
	}
	if (ctx.cfg.second.use_exceptions)
	{
		cmdline += " -fexceptions";
	}
	else
	{
		cmdline += " -fno-exceptions";
	}
	if (!for_linking)
	{
		for (auto& define : ctx.cfg.second.definitions)
		{
			cmdline += " -D" + define.first;
			if (!define.second.empty())
			{
				cmdline += "=" + define.second;
			}
		}
		for (auto& include_dir : ctx.cfg.second.additional_include_directories)
		{
			cmdline += " -I" + include_dir;
		}
	}
	if (ctx.trg.second.type == target_data::dynamic_library)
	{
		cmdline += " -fpic -shared";
	}
	if (ctx.cfg.second.use_debug_crt)
	{
		cmdline += " -D_GLIBCXX_DEBUG";
	}
	auto additional_opts = ctx.cfg.second.additional_toolchain_options.find(key);
	if (additional_opts != ctx.cfg.second.additional_toolchain_options.end())
	{
		for (auto& opt : additional_opts->second)
		{
			cmdline += opt;
		}
	}
	return cmdline;
}
