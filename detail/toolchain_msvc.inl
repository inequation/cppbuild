#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#include <Shlobj.h>
#include <Unknwn.h>
#include "win64/Setup.Configuration.h"

struct msvc : public toolchain
{
	constexpr static const char key[] = "msvc";

	static std::string get_object_for_cpptu(const std::string& source)
	{
		return cbl::path::get_path_without_extension(source.c_str()) + ".obj";
	}

	using discovered_toolchains = std::unordered_map<cbl::version, std::string>;
	
	static void discover_windows_sdks(
		discovered_toolchains& sdk_dirs,
		discovered_toolchains& crt_dirs
	)
	{
		using namespace cbl;
		using namespace win64::registry;

		// Enumerate the Windows 8.1 SDK, if present.
		std::string install_dir;
		
		install_dir.resize(MAX_PATH);
		if (try_read_software_path_key("Microsoft\\Microsoft SDKs\\Windows\\v8.1", "InstallationFolder", install_dir))
		{
			if (0 != cbl::fs::get_modification_timestamp(path::join(install_dir, "Include\\um\\windows.h").c_str()))
			{
				log(severity::verbose, "Found Windows 8.1 SDK at %s", install_dir.c_str());
				sdk_dirs[{ 8, 1 }] = install_dir;
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
				cbl::version number;
				if (number.parse(elements.back().c_str()))
				{
					if (0 != cbl::fs::get_modification_timestamp(path::join(dir, "um\\windows.h").c_str()))
					{
						log(severity::verbose, "Found Windows SDK %s at %s", number.to_string().c_str(), dir.c_str());
						sdk_dirs[number] = dir;
					}
					if (0 != cbl::fs::get_modification_timestamp(path::join(dir, "ucrt\\corecrt.h").c_str()))
					{
						log(severity::verbose, "Found Windows Universal CRT %s at %s", number.to_string().c_str(), dir.c_str());
						crt_dirs[number] = dir;
					}
				}
			}
		}
	}

	static cbl::version query_cl_exe_version(const char *path)
	{
		static constexpr const char header[] = "Microsoft (R) C/C++ Optimizing Compiler Version";

		cbl::version v;
		
		std::string buffer;
		auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
		{
			buffer.insert(buffer.end(), (const char *)data, ((const char *)data) + byte_count);
		};

		auto proc = cbl::process::start_async(path, append_to_buffer);

		if (0 == proc->wait() && 0 == strncmp(buffer.c_str(), header, sizeof(header) - 1))
			v.parse(buffer.c_str() + sizeof(header));
		else
			memset(&v, 0, sizeof(v));
		return v;
	}

	static void discover_compilers(discovered_toolchains& compiler_dirs)
	{
		using namespace cbl;
		using namespace win64::registry;

		// Enumerate the VS 2015 compiler, if present.
		std::string install_dir;
		version number;

		install_dir.resize(MAX_PATH);
		if (try_read_software_path_key("Microsoft\\VisualStudio\\SxS\\VS7", "14.0", install_dir))
		{
			number = query_cl_exe_version(("\"" + path::join(install_dir, "VC\\bin\\amd64\\cl.exe") + "\"").c_str());
			if (number.major != 0)
			{
				log(severity::verbose, "Found Visual C %s compiler at %s", number.to_string().c_str(), install_dir.c_str());
				compiler_dirs[number] = install_dir;
			}
		}

		static constexpr const char dll_name[] = "Microsoft.VisualStudio.Setup.Configuration.Native.dll";
		HMODULE vsscfg = LoadLibraryA(dll_name);
		if (!vsscfg)
		{
			PWSTR program_data;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &program_data)))
			{
				std::string path;
				if (win64::wide_str_to_ansi_str(path, program_data))
				{
					path = path::join(path, path::join("Microsoft\\VisualStudio\\Setup\\x64", dll_name));
					vsscfg = LoadLibraryA(path.c_str());
				}
				CoTaskMemFree(program_data);
			}
		}

		// This COM heresy is completely INSANE.
		if (vsscfg)
		{
			typedef HRESULT (STDMETHODCALLTYPE *PROC_GetSetupConfiguration)(
				_Out_ ISetupConfiguration** ppConfiguration,
				_Reserved_ LPVOID pReserved
				);
			if (auto* GetSetupConfiguration = (PROC_GetSetupConfiguration)GetProcAddress(vsscfg, "GetSetupConfiguration"))
			{
				ISetupConfiguration *setup = nullptr;
				if (SUCCEEDED(GetSetupConfiguration(&setup, nullptr)))
				{
					ISetupConfiguration2 *setup2;
					if (SUCCEEDED(setup->QueryInterface(IID_PPV_ARGS(&setup2))))
					{
						IEnumSetupInstances *esi;
						if (SUCCEEDED(setup2->EnumAllInstances(&esi)))
						{
							ULONG fetched;
							for (ISetupInstance *inst;;)
							{
								esi->Next(1, &inst, &fetched);
								if (!fetched)
									break;

								ISetupInstance2 *inst2;
								if (FAILED(inst->QueryInterface(IID_PPV_ARGS(&inst2))))
									continue;
								InstanceState state;
								if (FAILED(inst2->GetState(&state)) && !!(state & eLocal))
									continue;
								ISetupInstanceCatalog *catalog;
								if (FAILED(inst2->QueryInterface(IID_PPV_ARGS(&catalog))))
									continue;

								BSTR bstr;
								if (SUCCEEDED(inst2->GetInstallationVersion(&bstr)))
								{
									std::string version_str;
									if (cbl::win64::wide_str_to_ansi_str(version_str, bstr))
									{
										number.parse(version_str.c_str());
									}
									SysFreeString(bstr);
								}
								VARIANT_BOOL is_prerelease;
								if (catalog && SUCCEEDED(catalog->IsPrerelease(&is_prerelease)) && is_prerelease == -1)
								{
									strcpy(number.tag, "Prerelease");
								}
								
								if (FAILED(inst2->GetInstallationPath(&bstr)))
									continue;

								std::string path;
								if (win64::wide_str_to_ansi_str(path, bstr))
								{
									auto subdirs = fs::enumerate_directories(path::join(path.c_str(), "VC\\Tools\\MSVC\\*").c_str());
									for (const auto& dir : subdirs)
									{
										std::string cl_exe = "\"" + path::join(dir, "bin\\Hostx64\\x64\\cl.exe") + "\"";
										number = query_cl_exe_version(cl_exe.c_str());
										if (number.major != 0)
										{
											log(severity::verbose, "Found Visual C %s compiler at %s", number.to_string().c_str(), cl_exe.c_str());
											compiler_dirs[number] = dir;
										}
									}
								}
								SysFreeString(bstr);
							}
						}
					}
				}
			}
		}
	}

	struct toolchain_versions
	{
		cbl::version sdk, ucrt, compiler;
	};

	virtual void pick_toolchain_version(
		toolchain_versions& selection,
		const discovered_toolchains& sdks,
		const discovered_toolchains& ucrts,
		const discovered_toolchains& compilers
	)
	{
		using value_type = discovered_toolchains::value_type;
		auto sdk_it = std::max_element(sdks.begin(), sdks.end(),
			[](const value_type& a, const value_type& b)
			{ return a.first < b.first; }
		);
		if (sdk_it != sdks.end())
			selection.sdk = sdk_it->first;

		auto ucrt_it = std::max_element(ucrts.begin(), ucrts.end(),
			[](const value_type& a, const value_type& b)
			{ return a.first < b.first; }
		);
		if (ucrt_it != ucrts.end())
			selection.ucrt = ucrt_it->first;

		auto compiler_it = std::max_element(compilers.begin(), compilers.end(),
			[](const value_type& a, const value_type& b)
			{ return a.first < b.first; }
		);
		if (compiler_it != compilers.end())
			selection.compiler = compiler_it->first;
	}

	void initialize(const configuration& cfg) override
	{
		using elem = std::pair<cbl::version, std::string>;
		std::unordered_map<elem::first_type, elem::second_type> sdk_dirs, crt_dirs, compiler_dirs;
		discover_windows_sdks(sdk_dirs, crt_dirs);
		discover_compilers(compiler_dirs);

		toolchain_versions versions;
		pick_toolchain_version(versions, sdk_dirs, crt_dirs, compiler_dirs);
		sdk_dir = sdk_dirs[versions.sdk];
		ucrt_dir = crt_dirs[versions.ucrt];
		compiler_dir = compiler_dirs[versions.compiler];

		assert(!compiler_dir.empty());
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
				cmdline += " " + cbl::join(source_paths, " ");
				if (addtn_opts)
				{
					for (auto& o : *addtn_opts)
					{
						cmdline += " " + o;
					}
				}
				if (target.second.type == target_data::executable)
					cmdline += " /Fe" + target.second.output;
				cmdline += " /link";
				if (target.second.type != target_data::executable)
					cmdline += " /out:" + target.second.output;
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

		auto& cache = graph::get_timestamp_cache();

		auto it = cache.find(source);
		if (it != cache.end())
		{
			cbl::time::scoped_timer("Timestamp cache query", cbl::severity::verbose);
			bool up_to_date = true;
			for (const auto &entry : it->second)
			{
				uint64_t stamp = cbl::fs::get_modification_timestamp(entry.first.c_str());
				if (stamp == 0 || stamp != entry.second)
				{
					up_to_date = false;
					break;
				}
			}
			if (up_to_date)
			{
				for (const auto &entry : it->second)
				{
					push_dep(entry.first);
				}
				cbl::log(cbl::severity::verbose, "Timestamp cache HIT for TU %s", source.c_str());
				return;
			}
			else
			{
				cache.erase(it);
				cbl::log(cbl::severity::verbose, "Timestamp cache STALE for TU %s, discarded", source.c_str());
			}
		}

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
		auto p = cbl::process::start_async(cmdline.c_str(), append_to_buffer, append_to_buffer);
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

					push_dep(dep_name);
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
	std::string compiler_dir;

	std::string generate_cl_commandline_shared(const target& target, const configuration& cfg, const bool for_linking)
	{
		std::string cmdline = "\"" + cbl::path::join(compiler_dir, "bin\\Hostx64\\x64\\cl.exe") + "\"";
		cmdline += " /nologo";
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
			cmdline += " /I\"" + cbl::path::join(compiler_dir, "include") + "\"";
			if (!ucrt_dir.empty())
				cmdline += " /I\"" + cbl::path::join(ucrt_dir, "ucrt") + "\"";
			cmdline += " /I\"" + cbl::path::join(sdk_dir, "um") + "\"";
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
