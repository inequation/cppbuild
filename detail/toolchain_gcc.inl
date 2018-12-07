#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

struct gcc : public toolchain
{
	constexpr static const char key[] = "gcc";

	static std::string get_object_for_cpptu(const std::string& source)
	{
		return cbl::path::get_path_without_extension(source.c_str()) + ".o";
	}

	virtual void pick_toolchain_versions()
	{
		// FIXME: This is completely unportable.
		auto version = query_gcc_version("/usr/bin/g++");
		if (version.major == 0)
			abort();
		compiler_dir = "/usr/bin";
	}

	void initialize(const configuration& cfg) override
	{
		pick_toolchain_versions();
		
		if (compiler_dir.empty())
		{
			cbl::log(cbl::severity::error, "No compiler set. You might be able to compile code without Windows SDK, but not without a compiler.");
			abort();
		}
	}

	std::shared_ptr<cbl::process> invoke_compiler(
		const target& target,
		const std::string& object,
		const std::string& source,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override
	{
		std::string cmdline = generate_gcc_commandline_shared(target, cfg, false);
		cmdline += " -c -o " + object + " " + source;
		return cbl::process::start_async(cmdline.c_str(), on_stderr, on_stdout);
	}

	std::shared_ptr<cbl::process> invoke_linker(
		const target& target,
		const string_vector& source_paths,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override
	{
		const string_vector *addtn_opts = nullptr;
		{
			auto it = cfg.additional_toolchain_options.find("gcc link");
			if (it != cfg.additional_toolchain_options.end())
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
				if (cfg.use_debug_crt)
				{
					cmdline += " -lmcheck";
				}
				if (addtn_opts)
				{
					for (auto& o : *addtn_opts)
					{
						cmdline += " " + o;
					}
				}
				cmdline += " " + cbl::join(source_paths, " ");
				return cbl::process::start_async(cmdline.c_str());
			}
		default:
			assert(!"Unimplmented");
			// Call lib.exe for static libraries here.
			return nullptr;
		}
	}

	void generate_dependency_actions_for_cpptu(const target& target, const std::string& source, const configuration& cfg, std::vector<std::shared_ptr<graph::action>>& inputs)
	{
		auto push_dep = [&inputs](const std::string &name)
		{
			auto dep_action = std::make_shared<graph::cpp_action>();
			dep_action->type = (graph::action::action_type)graph::cpp_action::include;
			dep_action->outputs.push_back(name);
			inputs.push_back(dep_action);
		};

		if (graph::query_dependency_cache(source, push_dep))
			return;

		std::string cmdline = generate_gcc_commandline_shared(target, cfg, false);
		cmdline += " -c -M";
		std::vector<uint8_t> buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
		};
		cmdline += " " + source;
		
		auto p = cbl::process::start_async(cmdline.c_str(), append_to_buffer, append_to_buffer);
		if (p && 0 == p->wait())
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
			graph::insert_dependency_cache(source, deps);
		}

		graph::save_timestamp_cache();
	}

	std::shared_ptr<graph::action> generate_compile_action_for_cpptu(const target& target, const std::string& tu_path, const configuration& cfg) override
	{
		auto source = std::make_shared<graph::cpp_action>();
		source->type = (graph::action::action_type)graph::cpp_action::source;
		source->outputs.push_back(tu_path);
		generate_dependency_actions_for_cpptu(target, tu_path, cfg, source->inputs);

		auto action = std::make_shared<graph::cpp_action>();
		action->type = (graph::action::action_type)graph::cpp_action::compile;
		action->outputs.push_back(get_object_for_cpptu(tu_path));
		action->inputs.push_back(source);
		return action;
	}

	bool deploy_executable_with_debug_symbols(const char *existing_path, const char *new_path) override
	{
		using namespace cbl;
		return
			fs::copy_file(
				existing_path,
				new_path,
				fs::overwrite | fs::maintain_timestamps);
	}
	
	gcc()
	{}

protected:
	enum { component_sdk, component_ucrt, component_compiler, num_components };
	using discovered_components = std::unordered_map<cbl::version, std::string[2]>;

	static cbl::version query_gcc_version(const char *path)
	{
		static constexpr const char header[] = "gcc version ";

		cbl::version v;

		std::string buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (const char *)data, ((const char *)data) + byte_count);
		};

		auto proc = cbl::process::start_async((std::string(path) + " -v").c_str(), append_to_buffer, append_to_buffer);

		memset(&v, 0, sizeof(v));
		if (0 == proc->wait())
		{
			if (const char *vstr = strstr(buffer.c_str(), header))
				v.parse(vstr + sizeof(header) - 1);
		}
			
		return v;
	}

private:
	
	std::string include_dirs[num_components];
	std::string lib_dirs[num_components];
	std::string compiler_dir;

	std::string generate_gcc_commandline_shared(const target& target, const configuration& cfg, const bool for_linking)
	{
		std::string cmdline = "\"" + cbl::path::join(compiler_dir, "g++") + "\"";
		if (cfg.emit_debug_information)
		{
			cmdline += " -g";
		}
		if (cfg.optimize <= configuration::O3)
		{
			cmdline += " -O";
			cmdline += "0123"[cfg.optimize];
		}
		else if (cfg.optimize == configuration::Os)
		{
			cmdline += " -Os";
		}
		if (!for_linking)
		{
			for (auto& define : cfg.definitions)
			{
				cmdline += " -D" + define.first;
				if (!define.second.empty())
				{
					cmdline += "=" + define.second;
				}
			}
			for (auto& include_dir : cfg.additional_include_directories)
			{
				cmdline += " -I" + include_dir;
			}
		}
		if (target.second.type == target_data::dynamic_library)
		{
			cmdline += " -fpic -shared";
		}
		if (cfg.use_debug_crt)
		{
			cmdline += " -D_GLIBCXX_DEBUG";
		}
		auto additional_opts = cfg.additional_toolchain_options.find(key);
		if (additional_opts != cfg.additional_toolchain_options.end())
		{
			for (auto& opt : additional_opts->second)
			{
				cmdline += opt;
			}
		}
		return cmdline;
	}
};

// Storage.
constexpr const char gcc::key[];
