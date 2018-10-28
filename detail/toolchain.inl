#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

struct msvc : public toolchain
{
	constexpr static const char key[] = "msvc";

	static const char *get_vcvarsall_bat_path()
	{
		// FIXME: Actual detection!
		return R"(C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat)";
	}

	static std::string get_object_for_cpptu(const std::string& source)
	{
		return cbl::path::get_path_without_extension(source.c_str()) + ".obj";
	}

	void initialize(const configuration& cfg) override
	{
		/*std::string cmdline = "cmd /c \"\"";
		cmdline += get_vcvarsall_bat_path();
		cmdline += "\" ";
		switch (cfg.platform)
		{
		case platform::win64:
			cmdline += "x64";
			break;
		default:
			assert(0);
		}
		cmdline += " && cls && set\"";
		cbl::process::run(cmdline.c_str(), nullptr, &environment_block);
		if (!environment_block.empty())
		{
			environment_block.push_back(0);	// Ensure string null termination.
			const char *env = (char*)environment_block.data();
			if (const char *np = strchr(env, 0xC))
			{
				environment_block.erase(environment_block.begin(), environment_block.begin() + (np - env) + 1);
				env = (char*)environment_block.data();
			}
			while (const char *cr = strchr(env, '\r'))
			{
				environment_block.erase(environment_block.begin() + (cr - env));
				env = (char*)environment_block.data();
			}
			while (char *nl = const_cast<char *>(strchr(env, '\n')))
			{
				*nl = 0;
				env = nl + 1;
			}
			environment_block.push_back(0);	// Ensure block null termination.
			environment_block.shrink_to_fit();
		}*/
	}

	std::shared_ptr<cbl::process> invoke_compiler(
		const std::string& object,
		const std::string& source,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override
	{
		std::string cmdline = generate_cl_commandline_shared(source, cfg, false);
		cmdline += " /c /Fo" + object;
		return cbl::process::start_async(cmdline.c_str(), on_stderr, on_stdout, nullptr, environment_block.empty() ? nullptr : environment_block.data());
	}

	std::shared_ptr<cbl::process> invoke_linker(
		const target& target,
		const string_vector& source_paths,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override
	{
		switch (target.second.type)
		{
		case target_data::executable:
		case target_data::dynamic_library:
			{
				std::string cmdline = generate_cl_commandline_shared(cbl::join(source_paths, " "), cfg, true);
				cmdline += target.second.type == target_data::executable ? " /Fe" : " /link /out:";
				cmdline += target.first;
				return cbl::process::start_async(cmdline.c_str(), on_stderr, on_stdout, nullptr, environment_block.empty() ? nullptr : environment_block.data());
			}
		default:
			assert(!"Unimplmented");
			// Call lib.exe for static libraries here.
			return nullptr;
		}
	}

	void generate_dependency_actions_for_cpptu(const std::string& source, const configuration& cfg, std::vector<std::shared_ptr<graph::action>>& inputs)
	{
		std::string cmdline = generate_cl_commandline_shared(source, cfg, false) + " /c /showIncludes";
		std::vector<uint8_t> buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
		};
		// Documentation says "The /showIncludes option emits to stderr, not stdout", but that seems to be a lie.
		auto p = cbl::process::start_async(cmdline.c_str(), append_to_buffer, append_to_buffer, nullptr, environment_block.empty() ? nullptr : environment_block.data());
		if (p && 0 == p->wait())
		{
			constexpr const char needle[] = "Note: including file: ";
			const size_t needle_length = strlen(needle);
			buffer.push_back(0);	// Ensure null termination, so that we may treat data() as C string.
			const char *s = (char *)buffer.data();
			do
			{
				s = strstr(s, needle);
				if (s)
				{
					s += needle_length;
					const char *nl = strchr(s, '\n');
					if (!nl)
					{
						nl = (char*)&(buffer.back());
					}
					std::string dep_name(s, nl - s);
					cbl::trim(dep_name);

					// TODO: Cache! And filter out system headers!
					auto dep_action = std::make_shared<graph::cpp_action>();
					dep_action->type = (graph::action::action_type)graph::cpp_action::include;
					dep_action->outputs.push_back(dep_name);
					inputs.push_back(dep_action);
				}
			} while (s);
		}
	}

	std::shared_ptr<graph::action> generate_compile_action_for_cpptu(const std::string& tu_path, const configuration& cfg) override
	{
		auto source = std::make_shared<graph::cpp_action>();
		source->type = (graph::action::action_type)graph::cpp_action::source;
		source->outputs.push_back(tu_path);
		generate_dependency_actions_for_cpptu(tu_path, cfg, source->inputs);

		auto action = std::make_shared<graph::cpp_action>();
		action->type = (graph::action::action_type)graph::cpp_action::compile;
		action->outputs.push_back(get_object_for_cpptu(tu_path));
		action->inputs.push_back(source);
		return action;
	}
	
	msvc()
	{}

private:
	std::vector<uint8_t> environment_block;

	std::string generate_cl_commandline_shared(const std::string& input, const configuration& cfg, const bool for_linking)
	{
		std::string cmdline = "cl.exe /nologo";
		if (cfg.emit_debug_information)
		{
			cmdline += " /Zi";
		}
		if (cfg.optimize <= configuration::O3)
		{
			cmdline += " /O";
			cmdline += "0123"[cfg.optimize];
		}
		else if (cfg.optimize == configuration::Os)
		{
			cmdline += " /Os";
		}
		if (!for_linking)
		{
			for (auto& define : cfg.definitions)
			{
				cmdline += " /D" + define.first;
				if (!define.second.empty())
				{
					cmdline += "=" + define.second;
				}
			}
			for (auto& include_dir : cfg.additional_include_directories)
			{
				cmdline += " /I" + include_dir;
			}
		}
		auto additional_opts = cfg.additional_toolchain_options.find(key);
		if (additional_opts != cfg.additional_toolchain_options.end())
		{
			for (auto& opt : additional_opts->second)
			{
				cmdline += opt;
			}
		}
		cmdline += " " + input;
		return cmdline;
	}
};

void discover_toolchains(toolchain_map& toolchains)
{
	// FIXME: Actual discovery, detection etc.
	toolchains[msvc::key] = std::make_shared<msvc>();
}
