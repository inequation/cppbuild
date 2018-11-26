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
	
	static void detect_windows_sdks(
		std::unordered_map<cbl::version, std::string>& sdk_dirs,
		std::unordered_map<cbl::version, std::string>& crt_dirs
	)
	{
		using namespace cbl;
		using namespace win64::registry;

		// Enumerate the Windows 8.1 SDK, if present.
		std::string install_dir;
		
		install_dir.resize(MAX_PATH);
		if (try_read_software_path_key("Microsoft\\Microsoft SDKs\\Windows\\v8.1", "InstallationFolder", install_dir))
		{
			if (0 != cbl::fs::get_modification_timestamp(path::join(install_dir, path::join("Include", path::join("um", "windows.h"))).c_str()))
			{
				log(severity::verbose, "Found Windows 8.1 SDK at %s", install_dir.c_str());
				sdk_dirs[{ 8, 1, 0 }] = install_dir;
			}
		}

		// Find all the root directories for Windows 10 SDKs.
		string_vector roots;
		if (try_read_software_path_key("Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10", install_dir))
		{
			roots.push_back(install_dir);
		}
		if (try_read_software_path_key("Microsoft\\Microsoft SDKs\\Windows\\v10.0", "InstallationFolder", install_dir))
		{
			roots.push_back(install_dir);
		}

		// Enumerate all the Windows 10 SDKs.
		for (const auto& root : roots)
		{
			std::string include_dir = path::join(root, "Include");
			auto dirs = fs::enumerate_directories(path::join(include_dir, "*").c_str());
			for (const auto& dir : dirs)
			{
				auto elements = path::split(dir.c_str());
				cbl::version number{ 0, 0, 0 };
				if (0 < sscanf(elements.back().c_str(), "%zu.%zu.%zu.%*zu", &number.major, &number.minor, &number.patch))
				{
					if (0 != cbl::fs::get_modification_timestamp(path::join(dir, path::join("um", "windows.h")).c_str()))
					{
						log(severity::verbose, "Found Windows 10 SDK %zu at %s", number.patch, dir.c_str());
						sdk_dirs[number] = dir;
					}
					if (0 != cbl::fs::get_modification_timestamp(path::join(dir, path::join("ucrt", "corecrt.h")).c_str()))
					{
						log(severity::verbose, "Found Windows 10 Universal CRT %zu at %s", number.patch, dir.c_str());
						crt_dirs[number] = dir;
					}
				}
			}
		}
	}

	void initialize(const configuration& cfg) override
	{
		using elem = std::pair<cbl::version, std::string>;
		std::unordered_map<elem::first_type, elem::second_type> sdk_dirs, crt_dirs;
		detect_windows_sdks(sdk_dirs, crt_dirs);
		// FIXME: Enable SDK version choice instead of picking the newest.
		auto sdk_it = std::max_element(sdk_dirs.begin(), sdk_dirs.end(), [](const elem& a, const elem& b) { return a.first < b.first; });
		if (sdk_it != sdk_dirs.end())
			sdk_dir = sdk_it->second;
		auto ucrt_it = std::max_element(crt_dirs.begin(), crt_dirs.end(), [](const elem& a, const elem& b) { return a.first < b.first; });
		if (ucrt_it != crt_dirs.end())
			ucrt_dir = ucrt_it->second;
	}

	std::shared_ptr<cbl::process> invoke_compiler(
		const target& target,
		const std::string& object,
		const std::string& source,
		const configuration& cfg,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout) override
	{
		std::string cmdline = generate_cl_commandline_shared(target, cfg, false);
		cmdline += " /c /Fo" + object + " " + source;
		return cbl::process::start_async(cmdline.c_str(), on_stderr, on_stdout, nullptr, nullptr);
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
			auto it = cfg.additional_toolchain_options.find("msvc link");
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
				std::string cmdline = generate_cl_commandline_shared(target, cfg, true);
				cmdline += target.second.type == target_data::executable ? " /Fe" : " /link /out:";
				cmdline += target.second.output + " ";
				cmdline += cbl::join(source_paths, " ");
				if (addtn_opts)
				{
					for (auto& o : *addtn_opts)
					{
						cmdline += " " + o;
					}
				}
				return cbl::process::start_async(cmdline.c_str(), on_stderr, on_stdout, nullptr, nullptr);
			}
		default:
			assert(!"Unimplmented");
			// Call lib.exe for static libraries here.
			return nullptr;
		}
	}

	void generate_dependency_actions_for_cpptu(const target& target, const std::string& source, const configuration& cfg, std::vector<std::shared_ptr<graph::action>>& inputs)
	{
		auto& cache = graph::get_timestamp_cache();

		std::string cmdline = generate_cl_commandline_shared(target, cfg, false);
		cmdline += " /c /showIncludes /P /FiNUL";
		std::vector<uint8_t> buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
		};
		cmdline += " " + source;
		// Documentation says "The /showIncludes option emits to stderr, not stdout", but that seems to be a lie.
		// TODO: Cache!!! This doesn't need to run every single invocation!
		auto p = cbl::process::start_async(cmdline.c_str(), append_to_buffer, append_to_buffer, nullptr, nullptr);
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

			graph::dependency_timestamp_vector deps;
			for (const auto& i : inputs)
			{
				if (i->output_timestamps.empty())
				{
					i->update_output_timestamps();
				}
				deps.push_back(std::make_pair(i->outputs[0], i->output_timestamps[0]));
			}
			cache[source] = deps;
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
				cbl::fs::overwrite | cbl::fs::maintain_timestamps) &&
			fs::copy_file(
				(path::get_path_without_extension(existing_path) + ".pdb").c_str(),
				// This is not a typo - we want the basename of the original executable/DLL, as that's what debuggers will be looking for!
				(path::join(path::get_directory(new_path), path::get_basename(existing_path)) + ".pdb").c_str(),
				cbl::fs::overwrite | cbl::fs::maintain_timestamps);
	}
	
	msvc()
	{}

private:
	std::string sdk_dir;
	std::string ucrt_dir;

	std::string generate_cl_commandline_shared(const target& target, const configuration& cfg, const bool for_linking)
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
		if (target.second.type == target_data::executable)
		{
			cmdline += " /MT";
		}
		else
		{
			cmdline += " /MD";
		}
		if (cfg.use_debug_crt)
		{
			cmdline += 'd';
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
