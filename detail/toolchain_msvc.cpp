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

#if defined(_WIN64)

#include "toolchain_msvc.h"
#include "../cbl.h"

#include <Shlobj.h>
#include <Unknwn.h>
#include "win64/Setup.Configuration.h"

std::string msvc::get_object_for_cpptu(build_context &ctx, const char *source)
{
	return get_intermediate_path_for_cpptu(ctx, source, ".obj");
}

void msvc::pick_toolchain_versions()
{
	using elem = std::pair<version, std::string>;
	discovered_components sdks, ucrts, compilers;
	// FIXME: Parallelize.
	discover_windows_sdks(sdks, ucrts);
	discover_compilers(compilers);

	version sdk, ucrt, compiler;

	using value_type = discovered_components::value_type;
	auto sdk_it = std::max_element(sdks.begin(), sdks.end(),
		[](const value_type& a, const value_type& b)
		{ return a.first < b.first; }
	);
	if (sdk_it != sdks.end())
		sdk = sdk_it->first;

	auto ucrt_it = std::max_element(ucrts.begin(), ucrts.end(),
		[](const value_type& a, const value_type& b)
		{ return a.first < b.first; }
	);
	if (ucrt_it != ucrts.end())
		ucrt = ucrt_it->first;

	auto compiler_it = std::max_element(compilers.begin(), compilers.end(),
		[](const value_type& a, const value_type& b)
		{ return a.first < b.first; }
	);
	if (compiler_it != compilers.end())
		compiler = compiler_it->first;

	include_dirs[component_sdk] = sdks[sdk][0];
	lib_dirs[component_sdk] = sdks[sdk][1];
	include_dirs[component_ucrt] = ucrts[ucrt][0];
	lib_dirs[component_ucrt] = ucrts[ucrt][1];
	compiler_dir = compilers[compiler][0];
	include_dirs[component_compiler] = cbl::path::join(compiler_dir, "include");
	// Pre-2017 platform name is amd64, 2017+ uses x64.
	lib_dirs[component_compiler] = cbl::path::join(compiler_dir, "lib\\amd64");
	if (0 == cbl::fs::get_modification_timestamp(lib_dirs[component_compiler].c_str()))
		lib_dirs[component_compiler] = cbl::path::join(compiler_dir, "lib\\x64");

	cl_exe_path = "\"" + cbl::path::join(compiler_dir, "bin\\Hostx64\\x64\\cl.exe") + "\"";
}

bool msvc::initialize()
{
	pick_toolchain_versions();
		
	if (compiler_dir.empty())
	{
		cbl::log_verbose("No MSVC compiler set. You might be able to compile code without Windows SDK, but not without a compiler.");
		return false;
	}
	return true;
}

std::string msvc::generate_compiler_response(
	build_context &ctx,
	const char *object,
	const char *source)
{
	std::string cmdline = generate_cl_commandline_shared(ctx, false);
	cmdline += " /c /FS /Fo";
	cmdline += object;
	cmdline += ' ';
	cmdline += source;
	return cmdline;
}

std::string msvc::generate_linker_response(
	build_context &ctx,
	const char *product_path,
	const graph::action_vector& objects)
{
	const char *addtn_opts = nullptr;
	{
		auto it = ctx.cfg.second.additional_toolchain_options.find("msvc link");
		if (it != ctx.cfg.second.additional_toolchain_options.end())
			addtn_opts = it->second.c_str();
	}
	switch (ctx.trg.second.type)
	{
	case target_data::executable:
	case target_data::dynamic_library:
		{
			std::string cmdline = generate_cl_commandline_shared(ctx, true);
			for (auto &action : objects)
			{
				for (auto &output : action->outputs)
				{
					cmdline += ' ';
					cmdline += output;
				}
			}
			if (ctx.trg.second.type == target_data::executable)
			{
				cmdline += " /Fe";
				cmdline += product_path;
			}
			cmdline += " /link" + generate_system_library_directories();
			if (ctx.trg.second.type != target_data::executable)
			{
				cmdline += " /out:";
				cmdline += product_path;
			}
			if (addtn_opts)
				cmdline += addtn_opts;
			return cmdline;
		}
	default:
		assert(!"Unimplmented");
		// Call lib.exe for static libraries here.
		return "";
	}
}

void msvc::generate_dependency_actions_for_cpptu(
	build_context &ctx,
	const char *source,
	const char *response_file,
	const char *response,
	std::vector<std::shared_ptr<graph::action>>& inputs)
{
	auto push_dep = [&inputs](const std::string &name)
	{
		assert(!name.empty());
		auto dep_action = std::make_shared<graph::cpp_action>();
		dep_action->type = (graph::action::action_type)graph::cpp_action::include;
		dep_action->outputs.push_back(name);
		inputs.push_back(dep_action);
	};

	if (graph::query_dependency_cache(ctx, source, response, push_dep))
		return;

	std::string transient_definitions;
	for (auto& define : ctx.cfg.second.transient_definitions)
	{
		transient_definitions += " /D" + define.first;
		if (!define.second.empty())
		{
			transient_definitions += "=" + define.second;
		}
	}

	std::string cmdline = cl_exe_path;
	cmdline += transient_definitions;
	cmdline += " /c /showIncludes /E @";
	cmdline += response_file;
	
	std::vector<uint8_t> buffer;
	auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
	{
		buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + byte_count);
	};

	// Avoid JSON escape sequence issues.
	std::string safe_source = cbl::jsonify(source);
	MTR_SCOPE_S(__FILE__, "Dependency scan", "source", safe_source.c_str());
	int exit_code = cbl::process::start_sync(cmdline.c_str(), append_to_buffer, [](const void *, size_t) {});
	buffer.push_back(0);	// Ensure null termination, so that we may treat data() as C string.
	if (exit_code == 0)
	{
		constexpr const char needle[] = "Note: including file: ";
		const size_t needle_length = strlen(needle);
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
		graph::insert_dependency_cache(ctx, source, response, deps);
	}
	else
		cbl::fatal(exit_code, "%s: Dependency scan failed with code %d%s%s", source, exit_code,
			buffer.empty() ? "" : ", message:\n", buffer.empty() ? "" : (const char *)buffer.data());
}

cbl::deferred_process msvc::launch_cl_exe(const char *response, const char *additional_args)
{
	std::string cmdline = cl_exe_path;
	cmdline += " @";
	cmdline += response;
	if (additional_args)
		cmdline += additional_args;
	return cbl::process::start_deferred(cmdline.c_str());
}

cbl::deferred_process msvc::schedule_compiler(build_context &ctx, const char *response)
{
	std::string transient_definitions;
	for (auto& define : ctx.cfg.second.transient_definitions)
	{
		transient_definitions += " /D" + define.first;
		if (!define.second.empty())
		{
			transient_definitions += "=" + define.second;
		}
	}
	return launch_cl_exe(response, transient_definitions.c_str());
}

cbl::deferred_process msvc::schedule_linker(build_context &ctx, const char *response)
{
	return launch_cl_exe(response, nullptr);
}

bool msvc::deploy_executable_with_debug_symbols(
	const char *existing_path,
	const char *new_path)
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
	
msvc::msvc()
{}

void msvc::discover_windows_sdks(
	discovered_components& sdk_dirs,
	discovered_components& crt_dirs
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
			log_verbose("Found Windows 8.1 SDK at %s", install_dir.c_str());
			auto &entry = sdk_dirs[{ 8, 1 }];
			entry[0] = path::join(install_dir, "Include");
			entry[1] = path::join(install_dir, "Lib\\winv6.3");	// NOTE: This is for SDK 8.1, 8.0 would have this as winv8.0.
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
			version number;
			if (number.parse(elements.back().c_str()))
			{
				static auto conditional_add_entry = [](const char *name, const version &number, discovered_components& dirs, const char *base_dir, const char *tested_file)
				{
					if (0 != cbl::fs::get_modification_timestamp(path::join(base_dir, tested_file).c_str()))
					{
						log_verbose("Found %s %s at %s", name, number.to_string().c_str(), base_dir);
						auto &entry = dirs[number];
						entry[0] = base_dir;

						auto elements = cbl::path::split(base_dir);
						auto it = std::find_if(elements.rbegin(), elements.rend(), [](const std::string &s) { return 0 == stricmp(s.c_str(), "Include"); });
						if (it != elements.rend())
						{
							(*it) = "Lib";
						}
						entry[1] = cbl::path::join(elements);
					}
				};

				conditional_add_entry("Windows SDK", number, sdk_dirs, dir.c_str(), "um\\windows.h");
				conditional_add_entry("Windows Universal CRT", number, crt_dirs, dir.c_str(), "ucrt\\corecrt.h");
			}
		}
	}
}

version msvc::query_cl_exe_version(const char *path)
{
	static constexpr const char header[] = "Microsoft (R) C/C++ Optimizing Compiler Version";

	version v;

	std::string buffer;
	auto append_to_buffer = [&buffer](const void *data, size_t byte_count)
	{
		buffer.insert(buffer.end(), (const char *)data, ((const char *)data) + byte_count);
	};

	if (0 == cbl::process::start_sync(path, append_to_buffer, [](const void *, size_t) {}) &&
		0 == strncmp(buffer.c_str(), header, sizeof(header) - 1))
		v.parse(buffer.c_str() + sizeof(header));
	else
		memset(&v, 0, sizeof(v));
	return v;
}

void msvc::discover_compilers(msvc::discovered_components& compiler_dirs)
{
	using namespace cbl;
	using namespace win64::registry;

	// Enumerate the VS 2015 compiler, if present.
	std::string install_dir;
	version number;

	constexpr bool query_cl_exe = false;

	install_dir.resize(MAX_PATH);
	if (try_read_software_path_key("Microsoft\\VisualStudio\\SxS\\VS7", "14.0", install_dir))
	{
		if (query_cl_exe)
			number = query_cl_exe_version(("\"" + path::join(install_dir, "VC\\bin\\amd64\\cl.exe") + "\"").c_str());
		else
			number = version{ 14, 0 };
		if (number.major != 0)
		{
			log_verbose("Found Visual C %s compiler at %s", number.to_string().c_str(), install_dir.c_str());
			compiler_dirs[number][0] = install_dir;
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
			if (win64::wide_str_to_utf8_str(path, program_data))
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
		typedef HRESULT(STDMETHODCALLTYPE *PROC_GetSetupConfiguration)(
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
								if (cbl::win64::wide_str_to_utf8_str(version_str, bstr))
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
							if (win64::wide_str_to_utf8_str(path, bstr))
							{
								auto subdirs = fs::enumerate_directories(path::join(path.c_str(), "VC\\Tools\\MSVC\\*").c_str());
								for (const auto& dir : subdirs)
								{
									std::string cl_exe = "\"" + path::join(dir, "bin\\Hostx64\\x64\\cl.exe") + "\"";
									if (query_cl_exe)
										number = query_cl_exe_version(cl_exe.c_str());
									if (number.major != 0)
									{
										log_verbose("Found Visual C %s compiler at %s", number.to_string().c_str(), cl_exe.c_str());
										compiler_dirs[number][0] = dir;
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

std::string msvc::generate_system_include_directories()
{
	static constexpr const char prefix[] = " /I\"";
	static constexpr const char suffix[] = "\"";
	std::string cmdline = prefix + include_dirs[component_compiler] + suffix;
	if (!include_dirs[component_sdk].empty())
	{
		cmdline += prefix + cbl::path::join(include_dirs[component_sdk], "um") + suffix;
		cmdline += prefix + cbl::path::join(include_dirs[component_sdk], "shared") + suffix;
	}
	if (!include_dirs[component_ucrt].empty())
		cmdline += prefix + cbl::path::join(include_dirs[component_ucrt], "ucrt") + suffix;
	return cmdline;
}

std::string msvc::generate_system_library_directories()
{
	static constexpr const char prefix[] = " /LIBPATH:\"";
	static constexpr const char suffix[] = "\"";
	std::string cmdline = prefix + lib_dirs[component_compiler] + suffix;
	if (!lib_dirs[component_sdk].empty())
		cmdline += prefix + cbl::path::join(lib_dirs[component_sdk], "um\\x64") + suffix;
	if (!lib_dirs[component_ucrt].empty())
		cmdline += prefix + cbl::path::join(lib_dirs[component_ucrt], "ucrt\\x64") + suffix;
	return cmdline;
}

std::string msvc::generate_cl_commandline_shared(
	build_context &ctx,
	const bool for_linking)
{
	std::string cmdline = "/nologo";
	cmdline += " /std:c++";
	cmdline += std::to_string(int(ctx.cfg.second.standard));
	if (ctx.cfg.second.emit_debug_information)
	{
		cmdline += " /Zi";
	}
	if (ctx.cfg.second.optimize <= configuration_data::O3)
	{
		cmdline += " /O";
		cmdline += "0123"[ctx.cfg.second.optimize];
	}
	else if (ctx.cfg.second.optimize == configuration_data::Os)
	{
		cmdline += " /Os";
	}
	if (ctx.cfg.second.use_exceptions)
	{
		// Enable exception handling in C++, but not C code.
		cmdline += " /EHsc";
	}
	else
	{
		// Disable emission of exception code in MS's STL and system headers.
		cmdline += " /D_HAS_EXCEPTIONS=0";
	}
	if (!for_linking)
	{
		for (auto& define : ctx.cfg.second.definitions)
		{
			cmdline += " /D";
			cmdline += define.first;
			if (!define.second.empty())
			{
				cmdline += "=";
				cmdline += define.second;
			}
		}
		cmdline += generate_system_include_directories();
		for (auto& include_dir : ctx.cfg.second.additional_include_directories)
		{
			cmdline += " /I";
			cmdline += include_dir;
		}
	}
	if (ctx.trg.second.type == target_data::executable)
	{
		cmdline += " /MT";
	}
	else
	{
		cmdline += " /MD";
	}
	if (ctx.cfg.second.use_debug_crt)
	{
		cmdline += 'd';
	}
	auto additional_opts = ctx.cfg.second.additional_toolchain_options.find(key);
	if (additional_opts != ctx.cfg.second.additional_toolchain_options.end())
	{
		cmdline += additional_opts->second;
	}
	return cmdline;
}

#endif	// defined(_WIN64)
