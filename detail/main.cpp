#include "../cppbuild.h"
#include "../cbl.h"
#include "detail.h"

#include <sstream>

void dump_builds(const target_map& t, const configuration_map& c)
{
	MTR_SCOPE_FUNC();
	constexpr const char *types[] = { "Executable", "Static library", "Dynamic library" };
	static_assert(sizeof(types) / sizeof(types[0]) - 1 == target_data::dynamic_library, "Missing string for target type");

	std::ostringstream dump;
	dump << "Targets:\n";
	for (auto target : t)
	{
		dump << "\t" << types[target.second.type] << ' ' << target.first << ":\t\n"
			"\t{\n";
		auto sources = target.second.sources();
		for (auto source : sources)
		{
			dump << "\t\t" << source << "\n";
		}
		dump << "\t}\n";
	}

	constexpr const char *platforms[] = { "Windows 64-bit", "Linux 64-bit" };
	constexpr const char *booleans[] = { "false", "true" };
	static_assert(sizeof(platforms) / sizeof(platforms[0]) == size_t(platform::platform_count), "Missing string for platform");
	
	dump << "Available configurations:\n";
	for (auto cfg : c)
	{
		dump << "\t" << cfg.first << ":\t\n"
			"\t{\n";
		char opt_level = cfg.second.optimize == configuration_data::Os ? 's' : ('0' + (char)cfg.second.optimize);
		dump << "\t\tPlatform: " << platforms[(int)cfg.second.platform] << "\n"
			"\t\tEmit debug information: " << booleans[cfg.second.emit_debug_information] << "\n"
			"\t\tOptimization level: O" << opt_level << "\n"
			"\t\tDefinitions:\n"
			"\t\t{\n";
		for (auto& d : cfg.second.definitions)
		{
			dump << "\t\t\t" << d.first;
			if (!d.second.empty())
				dump << '=' << d.second;
			dump << '\n';
		}
		dump << "\t\t}\n"
			"\t\tAdditional include directories:\n"
			"\t\t{\n";
		for (auto& i : cfg.second.additional_include_directories)
		{
			dump << "\t\t\t" << i << '\n';
		}
		dump << "\t\t}\n"
			"\t\tAdditional toolchain options:\n"
			"\t\t{\n";
		for (auto& t : cfg.second.additional_toolchain_options)
		{
			dump << "\t\t\t\"" << t.first << "\": \"" << t.second << "\"\n";
		}
		dump << "\t\t}\n"
			"\t}\n";
	}

	cbl::info("Dumping described builds:\n%s", dump.str().c_str());
}

namespace detail
{
	void dump_action(std::ostringstream &dump, std::shared_ptr<graph::action> action, size_t indent)
	{
		constexpr char tab = ' ';//'\t';
		std::string tabs;
		for (size_t i = 0; i < indent; ++i)
		{
			tabs += tab;
		}

		if (!action)
		{
			dump << tabs << "Empty graph (up to date)";
			return;
		}

		const char *types[] =
		{
			"Link",
			"Compile",
			"Source",
			"Include",
		};
		static_assert(sizeof(types) / sizeof(types[0]) == graph::action::custom_actions_begin - 1, "Missing string for action type");
		dump << tabs << types[action->type] << '\n';
		dump << tabs << "{\n";
		tabs += tab;
		if (!action->outputs.empty())
		{
			dump << tabs << "Outputs:\n";
			dump << tabs << "{\n";
			for (const auto& s : action->outputs)
				dump << tabs << tab << s << '\n';
			dump << tabs << "}\n";
		}
		if (!action->inputs.empty())
		{
			dump << tabs << "Inputs:\n";
			dump << tabs << "{\n";
			for (const auto& i : action->inputs)
				dump_action(dump, i, indent + 2);
			dump << tabs << "}\n";
		}
		tabs.pop_back();
		dump << tabs << "}\n";
	}
};

void dump_graph(std::shared_ptr<graph::action> root)
{
	MTR_SCOPE_FUNC();
	std::ostringstream dump;
	detail::dump_action(dump, root, 0);
	cbl::info("Dumping build graph:\n%s", dump.str().c_str());
}

std::pair<build_context, std::shared_ptr<graph::action>> setup_build(target& target, const configuration& cfg, toolchain_map& toolchains)
{
	const char *used_tc = target.second.used_toolchain;
	if (!used_tc)
	{
		used_tc = cbl::get_default_toolchain_for_host();
	}

	if (cbl::path::get_extension(target.second.output.c_str()).empty())
		target.second.output += cbl::get_default_extension_for_product(
			target.second.type, cfg.second.platform);

	MTR_SCOPE_FUNC_S("Used toolchain", used_tc);

	assert(toolchains.find(used_tc) != toolchains.end() && "Unknown toolchain");
	auto& tc = toolchains[used_tc];

	build_context ctx{ target, cfg, *tc };
	return { ctx, graph::generate_cpp_build_graph(ctx) };
}

void cull_build(build_context& ctx, std::shared_ptr<graph::action>& root)
{
	if (g_options.dump_graph.val.as_int32 > 1)
	{
		cbl::info("Before culling:");
		dump_graph(std::static_pointer_cast<graph::cpp_action>(root));
	}

	graph::cull_build_graph(ctx, root);

	if (g_options.dump_graph.val.as_int32 > 0)
	{
		cbl::info("After culling:");
		dump_graph(std::static_pointer_cast<graph::cpp_action>(root));
	}

	graph::save_timestamp_caches();
}

int execute_build(build_context& ctx, std::shared_ptr<graph::action> root)
{
	int exit_code = root
		? graph::execute_build_graph(ctx, root)
		: (cbl::info("Target %s up to date", ctx.trg.first.c_str()), 0);
		
	cbl::info("Build finished with code %d", exit_code);
	return exit_code;
}

namespace bootstrap
{
	std::pair<target, configuration> describe(toolchain_map& toolchains)
	{
		using namespace cbl;

		constexpr char cppbuild[] = "build";

		target_data target;
		target.output = path::join(path::get_cppbuild_cache_path(), "bin", cppbuild);
		target.type = target_data::executable;
		target.sources = []()
		{
			auto detail_path = path::join("cppbuild", "detail");
			auto sources = fs::enumerate_files(path::join(detail_path, "*.cpp").c_str());
			sources.emplace_back(path::join(detail_path, "enkiTS", "src", "TaskScheduler.cpp"));
			sources.emplace_back(path::join(detail_path, "minitrace", "minitrace.c"));
			sources.emplace_back(path::join(detail_path, "getopt", "getopt.c"));
			sources.emplace_back(path::join(detail_path, "getopt", "getopt_long.c"));
			sources.emplace_back("build.cpp");
			return sources;
		};
		target.used_toolchain = cbl::get_default_toolchain_for_host();

		configuration cfg;
		cfg.first = "bootstrap";
		cfg.second = base_configurations::debug(cbl::get_host_platform());
		cfg.second.standard = configuration_data::Cxx14;
		cfg.second.additional_include_directories.push_back("cppbuild");
		cfg.second.definitions.push_back(std::make_pair("MTR_ENABLED", "1"));
		// cppbuild generation needs to be transient, otherwise the build-deploy loop never ends.
		cfg.second.transient_definitions.push_back(std::make_pair("CPPBUILD_GENERATION", std::to_string(CPPBUILD_GENERATION == 0 ? 2 : (CPPBUILD_GENERATION + 1))));
		cfg.second.additional_toolchain_options["msvc link"] += " /SUBSYSTEM:CONSOLE";
		cfg.second.additional_toolchain_options["gcc link"] += " -pthread";

		return std::make_pair(
			std::make_pair(cppbuild, target),
			cfg
		);
	}

	int build(toolchain_map& toolchains, int argc, const char *argv[])
	{
		MTR_SCOPE(__FILE__, "cppbuild bootstrapping");
		using namespace cbl;

		auto bootstrap = describe(toolchains);

		auto build = setup_build(bootstrap.first, bootstrap.second, toolchains);
#if CPPBUILD_GENERATION > 0
		// Only cull the build graph once we have successfully bootstrapped.
		cull_build(build.first, build.second);
#endif

		std::string staging_dir = path::join(path::get_cppbuild_cache_path(), "bin");
		if (build.second)
		{
			time::scoped_timer _("Rebuild outdated cppbuild executable");
			int exit_code = execute_build(build.first, build.second);
			if (exit_code == 0)
			{
				MTR_SCOPE(__FILE__, "cppbuild deployment dispatch");
				std::string cmdline = bootstrap.first.second.output;
				cmdline += " --bootstrap-deploy="
					+ std::to_string(cbl::process::get_current_pid()) + ","
					+ "\"" + cbl::process::get_current_executable_path() + "\","
					+ bootstrap.first.second.used_toolchain;
				// Pass in any extra arguments we may have received.
				for (int i = 1; i < argc; ++i)
				{
					cmdline += ' ';
					cmdline += argv[i];
				}
				// Make sure the trace file is flushed so that concatenation doesn't corrupt it.
				mtr_flush();
				auto p = cbl::process::start_async(cmdline.c_str());
				if (!p)
					fatal(int(error_code::failed_bootstrap_respawn), "Failed to bootstrap cppbuild, command line %s", cmdline.c_str());
				p->detach();
				exit(0);
			}
			return exit_code;
		}
		else
		{
			info("cppbuild executable up to date");
			MTR_SCOPE(__FILE__, "Garbage collection");
			string_vector garbage = string_vector{ "build.obj", "build.o", "build.ilk" };
			for (auto& g : garbage)
			{
				if (0 != fs::get_modification_timestamp(g.c_str()))
					fs::delete_file(g.c_str());
			}
			return 0;
		}
	}

	int deploy(int argc, char *argv[], const toolchain_map& toolchains)
	{
		using namespace cbl;
		std::string logged_params = g_options.bootstrap_deploy.val.as_str_ptr;
		// Avoid JSON escape sequence issues.
		for (auto& c : logged_params) { if (c == '\\') c = '/'; }
		MTR_SCOPE_FUNC_S("Deployment params", logged_params.c_str());

		// Finish the bootstrap deployment:
		assert(nullptr != g_options.bootstrap_deploy.val.as_str_ptr);
		string_vector params = split(g_options.bootstrap_deploy.val.as_str_ptr, ',');
		if (params.size() == 3)
		{
			// Params' format: <parent pid>,<original cppbuild executable>,<toolchain used>
			uint32_t parent_pid = atoi(params[0].c_str());
			process::wait_for_pid(parent_pid);
			auto it = toolchains.find(params[2].c_str());
			if (it == toolchains.end())
			{
				return (int)error_code::failed_bootstrap_bad_toolchain;
			}
			std::shared_ptr<toolchain> tc = it->second;
			if (tc->deploy_executable_with_debug_symbols(
				process::get_current_executable_path().c_str(),
				params[1].c_str()))
			{
				info("Successful bootstrap deployment");
				std::string cmdline = params[1];
				cmdline += " --append-logs";
				for (int i = 3; i < argc; ++i)
				{
					cmdline += ' ';
					cmdline += argv[i];
				}
				auto p = process::start_async(cmdline.c_str());
				if (p)
				{
					p->detach();
					return 0;
				}
				else
				{
					error("Failed to respawn after deployment");
					return (int)error_code::failed_bootstrap_respawn;
				}
			}
			else
			{
				error("Failed to overwrite the cppbuild executable");
				return (int)error_code::failed_bootstrap_deployment;
			}
		}
		else
		{
			error("Bad deployment parameters: '%s'", g_options.bootstrap_deploy.val.as_str_ptr);
			return (int)error_code::failed_bootstrap_deployment;
		}
	}
};

int main(int argc, char *argv[])
{
	init_process_group();

	int first_non_opt_arg = parse_args(argc, const_cast<const char **>(argv));

	if (g_options.version.val.as_bool)
	{
		print_version();
		exit(0);
	}

	if (g_options.help.val.as_bool)
	{
		print_usage(argv[0]);
		exit(0);
	}

	const bool append = g_options.append_logs.val.as_bool || g_options.bootstrap_deploy.val.as_bool;
	rotate_traces(append);
	if (g_options.jobs.val.as_int32 > 0)
		cbl::scheduler.Initialize(g_options.jobs.val.as_int32);
	else
		cbl::scheduler.Initialize();
	rotate_logs(append);

	cppbuild::background_delete delete_old_logs_and_traces;
	cbl::scheduler.AddTaskSetToPipe(&delete_old_logs_and_traces);

	cbl::scoped_guard cleanup([](){ cbl::scheduler.WaitforAllAndShutdown(); });

	toolchain_map toolchains;
	discover_toolchains(toolchains);

	if (g_options.bootstrap_deploy.val.as_str_ptr)
	{
		return bootstrap::deploy(argc - first_non_opt_arg, argv + first_non_opt_arg, toolchains);
	}

	// If we were in need of bootstrapping, this call will terminate the process.
	if (0 != bootstrap::build(toolchains, argc - first_non_opt_arg, const_cast<const char**>(argv + first_non_opt_arg)))
	{
		cbl::error("FATAL: Failed to bootstrap cppbuild");
		return (int)error_code::failed_bootstrap_build;
	}

	target_map targets;
	configuration_map configs;

	MTR_BEGIN(__FILE__, "describe");
	auto arguments = describe(targets, configs, toolchains);
	MTR_END(__FILE__, "describe");

	if (g_options.dump_builds.val.as_bool)
		dump_builds(targets, configs);

	// Read target and configuration info from command line.
	if (first_non_opt_arg < argc)
	{
		arguments.first = argv[first_non_opt_arg];
	}
	if (first_non_opt_arg + 1 < argc)
	{
		arguments.second = argv[first_non_opt_arg + 1];
	}

	auto it = targets.find(arguments.first);
	if (it == targets.end())
	{
		cbl::error("Unknown target %s", arguments.first.c_str());
		return (int)error_code::unknown_target;
	}
	auto cfg = configs.find(arguments.second);
	if (cfg == configs.end())
	{
		cbl::error("Unknown configuration %s", arguments.second.c_str());
		return (int)error_code::unknown_configuration;
	}

	MTR_SCOPE_S(__FILE__, "Building target", "target", it->first.c_str());
	std::string desc = "Building target " + it->first + " in configuration " + cfg->first;
	cbl::time::scoped_timer _(desc.c_str());

	// Apparently, iterator does not create a reference to the item. GCC deletes the contents of targets after the call to setup_build().
	target local_copy{ *it };

	auto build = setup_build(local_copy, *cfg, toolchains);
	cull_build(build.first, build.second);
	return execute_build(build.first, build.second);
}
