#include "toolchain_gcc.h"
#include "../cbl.h"

// Storage.
constexpr const char gcc::key[];

std::string gcc::get_object_for_cpptu(
	const std::string& source,
	const target &target,
	const configuration &cfg)
{
	return toolchain::get_intermediate_path_for_cpptu(source.c_str(), ".o", target, cfg);
}

void gcc::pick_toolchain_versions()
{
	// FIXME: This is completely unportable.
	auto version = query_gcc_version("/usr/bin/g++");
	compiler_dir = version.major != 0 ? "/usr/bin" : "";
}

bool gcc::initialize()
{
	pick_toolchain_versions();
		
	if (compiler_dir.empty())
	{
		cbl::log_verbose("GCC binary not found.");
		return false;
	}
	return true;
}

cbl::deferred_process gcc::schedule_compiler(
	const target& target,
	const std::string& object,
	const std::string& source,
	const configuration& cfg,
	const cbl::pipe_output_callback& on_stderr,
	const cbl::pipe_output_callback& on_stdout)
{
	std::string cmdline = generate_gcc_commandline_shared(target, cfg, false);
	cmdline += " -c -o " + object + " " + source;
	return cbl::process::start_deferred(cmdline.c_str(), on_stderr, on_stdout);
}

cbl::deferred_process gcc::schedule_linker(
	const target& target,
	const string_vector& source_paths,
	const configuration& cfg,
	const cbl::pipe_output_callback& on_stderr,
	const cbl::pipe_output_callback& on_stdout)
{
	const string_vector *addtn_opts = nullptr;
	{
		auto it = cfg.second.additional_toolchain_options.find("gcc link");
		if (it != cfg.second.additional_toolchain_options.end())
		{
			addtn_opts = &it->second;
		}
	}
	switch (target.second.type)
	{
	case target_data::executable:
	case target_data::dynamic_library:
		{
			std::string cmdline = generate_gcc_commandline_shared(target, cfg, true);
			cmdline += " -o " + target.second.output;
			if (cfg.second.use_debug_crt)
			{
				// FIXME: mcheck is not thread-safe without additional synchronisation.
				//cmdline += " -lmcheck";
			}
			if (addtn_opts)
			{
				for (auto& o : *addtn_opts)
				{
					cmdline += " " + o;
				}
			}
			cmdline += " " + cbl::join(source_paths, " ");
			return cbl::process::start_deferred(cmdline.c_str());
		}
	default:
		assert(!"Unimplmented");
		// Call lib.exe for static libraries here.
		return nullptr;
	}
}

void gcc::generate_dependency_actions_for_cpptu(
	const target& target,
	const std::string& source,
	const configuration& cfg,
	std::vector<std::shared_ptr<graph::action>>& inputs)
{
	auto push_dep = [&inputs](const std::string &name)
	{
		auto dep_action = std::make_shared<graph::cpp_action>();
		dep_action->type = (graph::action::action_type)graph::cpp_action::include;
		dep_action->outputs.push_back(name);
		inputs.push_back(dep_action);
	};

	if (graph::query_dependency_cache(target, cfg, source, push_dep))
		return;

	std::string cmdline = generate_gcc_commandline_shared(target, cfg, false);
	cmdline += " -c -M";
	std::vector<uint8_t> buffer;
	auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
	{
		buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
	};
	cmdline += " " + source;
		
	// Avoid JSON escape sequence issues.
	std::string safe_source = source;
	for (auto& c : safe_source) { if (c == '\\') c = '/'; }
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
			if (0 == strncmp(s, source.c_str(), source.size()))
				s += source.size() + 1;

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
		graph::insert_dependency_cache(target, cfg, source, deps);
	}
	else
		// FIXME: Find a proper way to report this and stop the build.
		cbl::error("%s: Dependency scan failed with code %d%s%s", source, exit_code,
			buffer.empty() ? "" : ", message:\n", buffer.empty() ? "" : (const char *)buffer.data());
}

std::shared_ptr<graph::action> gcc::generate_compile_action_for_cpptu(
	const target& target,
	const std::string& tu_path,
	const configuration& cfg)
{
	auto source = std::make_shared<graph::cpp_action>();
	source->type = (graph::action::action_type)graph::cpp_action::source;
	source->outputs.push_back(tu_path);
	generate_dependency_actions_for_cpptu(target, tu_path, cfg, source->inputs);

	auto action = std::make_shared<graph::cpp_action>();
	action->type = (graph::action::action_type)graph::cpp_action::compile;
	action->outputs.push_back(get_object_for_cpptu(tu_path, target, cfg));
	action->inputs.push_back(source);
	return action;
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
	const target& target,
	const configuration& cfg,
	const bool for_linking)
{
	std::string cmdline = "\"" + cbl::path::join(compiler_dir, "g++") + "\"";
	if (cfg.second.emit_debug_information)
	{
		cmdline += " -g";
	}
	if (cfg.second.optimize <= configuration_data::O3)
	{
		cmdline += " -O";
		cmdline += "0123"[cfg.second.optimize];
	}
	else if (cfg.second.optimize == configuration_data::Os)
	{
		cmdline += " -Os";
	}
	if (!for_linking)
	{
		for (auto& define : cfg.second.definitions)
		{
			cmdline += " -D" + define.first;
			if (!define.second.empty())
			{
				cmdline += "=" + define.second;
			}
		}
		for (auto& include_dir : cfg.second.additional_include_directories)
		{
			cmdline += " -I" + include_dir;
		}
	}
	if (target.second.type == target_data::dynamic_library)
	{
		cmdline += " -fpic -shared";
	}
	if (cfg.second.use_debug_crt)
	{
		cmdline += " -D_GLIBCXX_DEBUG";
	}
	auto additional_opts = cfg.second.additional_toolchain_options.find(key);
	if (additional_opts != cfg.second.additional_toolchain_options.end())
	{
		for (auto& opt : additional_opts->second)
		{
			cmdline += opt;
		}
	}
	return cmdline;
}
