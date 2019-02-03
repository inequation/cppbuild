#include "../cppbuild.h"
#include "../cbl.h"
#include "detail.h"
#include "cbl_detail.h"

void dump_builds(const target_map& t, const configuration_map& c)
{
	MTR_SCOPE("main", "dump_builds");
	constexpr const char *types[] = { "Executable", "Static library", "Dynamic library" };
	static_assert(sizeof(types) / sizeof(types[0]) - 1 == target_data::dynamic_library, "Missing string for target type");

	printf("Targets:\n");
	for (auto target : t)
	{
		printf("\t%s %s:\t\n"
			"\t{\n",
			types[target.second.type],
			target.first.c_str());
		auto sources = target.second.sources();
		for (auto source : sources)
		{
			printf("\t\t%s\n", source.c_str());
		}
		printf("\t}\n");
	}

	constexpr const char *platforms[] = { "Windows 64-bit", "Linux 64-bit", "macOS", "PS4", "Xbox One" };
	constexpr const char *booleans[] = { "false", "true" };
	static_assert(sizeof(platforms) / sizeof(platforms[0]) - 1 == (size_t)platform::xbox1, "Missing string for platform");
	
	printf("Available configurations:\n");
	for (auto cfg : c)
	{
		printf("\t%s:\t\n"
			"\t{\n",
			cfg.first.c_str());
		printf("\t\tPlatform: %s\n"
			"\t\tEmit debug information: %s\n"
			"\t\tOptimization level: O%s\n"
			"\t\tDefinitions:\n"
			"\t\t{\n",
			platforms[(int)cfg.second.platform],
			booleans[cfg.second.emit_debug_information],
			cfg.second.optimize == configuration_data::Os ? "s" : std::to_string((int)cfg.second.optimize).c_str());
		for (auto& d : cfg.second.definitions)
		{
			printf("\t\t\t%s%s%s\n",
				d.first.c_str(),
				d.second.empty() ? "" : "=",
				d.second.empty() ? "" : d.second.c_str());
		}
		printf("\t\t}\n"
			"\t\tAdditional include directories:\n"
			"\t\t{\n");
		for (auto& i : cfg.second.additional_include_directories)
		{
			printf("\t\t\t%s\n", i.c_str());
		}
		printf("\t\t}\n"
			"\t\tAdditional toolchain options:\n"
			"\t\t{\n");
		for (auto& t : cfg.second.additional_toolchain_options)
		{
			printf("\t\t\t\"%s\" =\n"
				"\t\t\t{",
				t.first.c_str());
			for (auto& o : t.second)
			{
				printf("\t\t\t\t\"%s\"\n", o.c_str());
			}
		}
		printf("\t\t}\n"
			"\t}\n");
	}
}

namespace detail
{
	void dump_action(std::shared_ptr<graph::action> action, size_t indent)
	{
		constexpr char tab = ' ';//'\t';
		constexpr char tab_s[2] = { tab, 0 };
		std::string tabs;
		for (size_t i = 0; i < indent; ++i)
		{
			tabs += tab;
		}

		if (!action)
		{
			printf("%sEmpty graph (up to date)\n",
				tabs.c_str());
			return;
		}

		const char *types[] =
		{
			"Link",
			"Compile",
			"Source",
			"Include",
			"Generate"
		};
		static_assert(sizeof(types) / sizeof(types[0]) == graph::action::cpp_actions_end, "Missing string for action type");
		printf("%s%s\n"
			"%s{\n",
			tabs.c_str(), types[action->type],
			tabs.c_str());
		if (!action->outputs.empty())
		{
			printf("%s%cOutputs:\n"
				"%s%c{\n",
				tabs.c_str(), tab,
				tabs.c_str(), tab);
			for (const auto& s : action->outputs)
			{
				printf("%s%c%c%s\n",
					tabs.c_str(), tab, tab, s.c_str());
			}
			printf("%s%c}\n",
				tabs.c_str(), tab);
		}
		if (!action->inputs.empty())
		{
			printf("%s%cInputs:\n"
				"%s%c{\n",
				tabs.c_str(), tab,
				tabs.c_str(), tab);
			for (const auto& i : action->inputs)
			{
				dump_action(i, indent + 2);
			}
			printf("%s%c}\n",
				tabs.c_str(), tab);
		}
		printf("%s}\n",
			tabs.c_str());
	}
};

void dump_graph(std::shared_ptr<graph::action> root)
{
	MTR_SCOPE("main", "dump_graph");
	detail::dump_action(root, 0);
}

std::pair<std::shared_ptr<graph::action>, std::shared_ptr<toolchain>> setup_build(const target& target, const configuration& cfg, toolchain_map& toolchains, std::shared_ptr<toolchain> *out_tc = nullptr)
{
	const char *used_tc = target.second.used_toolchain;
	if (!used_tc)
	{
		used_tc = cbl::get_default_toolchain_for_host();
	}

	MTR_SCOPE_S("build", "setup", "used_toolchain", used_tc);

	assert(toolchains.find(used_tc) != toolchains.end() && "Unknown toolchain");
	auto& tc = toolchains[used_tc];

	return std::make_pair(
		graph::generate_cpp_build_graph(target, cfg, tc),
		tc
	);
}

void cull_build(std::shared_ptr<graph::action>& root)
{
	// TODO: Also compare toolchain invocations, as they may easily change the output.
	/*cbl::info("Build graph before culling:");
	dump_graph(std::static_pointer_cast<graph::cpp_action>(root));*/

	graph::cull_build_graph(root);
	/*cbl::info("Build graph after culling:");
	dump_graph(std::static_pointer_cast<graph::cpp_action>(root));*/

	graph::save_timestamp_caches();
}

int execute_build(const target& target, std::shared_ptr<graph::action> root, const configuration& cfg, std::shared_ptr<toolchain> tc)
{
	int exit_code = root
		? graph::execute_build_graph(target, root, cfg, tc, nullptr, nullptr)
		: (cbl::info("Target %s up to date", target.first.c_str()), 0);
		
	cbl::info("Build finished with code %d", exit_code);
	return exit_code;
}

namespace bootstrap
{
	std::pair<target, configuration> describe(toolchain_map& toolchains)
	{
		using namespace cbl;

		constexpr char cppbuild[] = "build"
#if defined(_WIN64)
			".exe"
#endif
			;

		target_data target;
		target.output = cppbuild;
		target.type = target_data::executable;
		target.sources = []()
		{
			auto detail_path = path::join("cppbuild", "detail");
			auto sources = fs::enumerate_files(path::join(detail_path, "*.cpp").c_str());
			sources.emplace_back(path::join(detail_path, path::join("enkiTS", path::join("src", "TaskScheduler.cpp"))));
			sources.emplace_back(path::join(detail_path, path::join("minitrace", "minitrace.c")));
			sources.emplace_back(path::join(detail_path, path::join("getopt", "getopt.c")));
			sources.emplace_back(path::join(detail_path, path::join("getopt", "getopt_long.c")));
			sources.emplace_back("build.cpp");
			return sources;
		};
		target.used_toolchain = cbl::get_default_toolchain_for_host();

		configuration cfg;
		cfg.first = "bootstrap";
		cfg.second = base_configurations::debug(cbl::get_host_platform());
		cfg.second.additional_include_directories.push_back("cppbuild");
		cfg.second.definitions.push_back(std::make_pair("CPPBUILD_GENERATION", std::to_string(CPPBUILD_GENERATION == 0 ? 2 : (CPPBUILD_GENERATION + 1))));
		cfg.second.definitions.push_back(std::make_pair("MTR_ENABLED", "1"));
		cfg.second.additional_toolchain_options["msvc link"] = cbl::vwrap("/SUBSYSTEM:CONSOLE");
		cfg.second.additional_toolchain_options["gcc link"] = cbl::vwrap("-pthread");

		return std::make_pair(
			std::make_pair(cppbuild, target),
			cfg
		);
	}

	int build(toolchain_map& toolchains, int argc, const char *argv[])
	{
		MTR_SCOPE("bootstrap", "bootstrap_build");
		using namespace cbl;

		auto bootstrap = describe(toolchains);

		auto build = setup_build(bootstrap.first, bootstrap.second, toolchains);
#if CPPBUILD_GENERATION > 0	// Only cull the build graph once we have successfully bootstrapped.
		cull_build(build.first);
#endif

		std::string staging_dir = path::join(path::get_cppbuild_cache_path(), "bin");
		if (build.first)
		{
			fs::mkdir(staging_dir.c_str(), true);

			// Now here comes the hack: we rename the output of the root node *only after the culling*.
			// I.e. cull based on main cppbuild executable's timestamp, but output to a file named differently.
			std::string new_cppbuild = path::join(staging_dir, "build-");
			new_cppbuild += std::to_string(process::get_current_pid());
#if defined(_WIN64)
			new_cppbuild += ".exe";
#endif
			build.first->outputs[0] = new_cppbuild;
			bootstrap.first.second.output = new_cppbuild;

			time::scoped_timer _("Bootstrap outdated cppbuild executable");
			int exit_code = execute_build(bootstrap.first, build.first, bootstrap.second, build.second);
			if (exit_code == 0)
			{
				MTR_SCOPE("bootstrap", "bootstrap_deploy_dispatch");
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
				auto p = cbl::process::start_async(cmdline.c_str());
				if (!p)
				{
					error("FATAL: Failed to bootstrap cppbuild, command line %s", cmdline.c_str());
					abort();
				}
				p->detach();
				exit(0);
			}
			return exit_code;
		}
		else
		{
			MTR_SCOPE("bootstrap", "gc");
			info("cppbuild executable up to date");
			auto old_copies = cbl::fs::enumerate_files(cbl::path::join(staging_dir, "build-*").c_str());
#ifdef _WIN64
			// Don't cannibalise our own debug info!
			cbl::win64::debug::filter_own_pdb(old_copies);
#endif
			for (auto& o : old_copies)
			{
				cbl::fs::delete_file(o.c_str());
			}
			return 0;
		}
	}

	int deploy(int argc, char *argv[], const toolchain_map& toolchains)
	{
		using namespace cbl;
		MTR_SCOPE_FUNC_S("deployment_params", g_options.bootstrap_deploy.val.as_str_ptr);
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
	int first_non_opt_arg = parse_args(argc, argv);

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
	cbl::detail::rotate_traces(append);
	if (g_options.jobs.val.as_int32 > 0)
		cbl::scheduler.Initialize(g_options.jobs.val.as_int32);
	else
		cbl::scheduler.Initialize();
	cbl::detail::rotate_logs(append);

	cbl::detail::background_delete delete_old_logs_and_traces;
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

	MTR_BEGIN("main", "describe");
	auto arguments = describe(targets, configs, toolchains);
	MTR_END("main", "describe");
	//dump_builds(targets, configs);

	// Read target and configuration info from command line.
	if (first_non_opt_arg < argc)
	{
		arguments.first = argv[first_non_opt_arg];
	}
	if (first_non_opt_arg + 1 < argc)
	{
		arguments.second = argv[first_non_opt_arg + 1];
	}

	auto target = targets.find(arguments.first);
	if (target == targets.end())
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

	MTR_SCOPE_S("build", "build_target", "target", target->first.c_str());
	std::string desc = "Building target " + target->first + " in configuration " + cfg->first;
	cbl::time::scoped_timer _(desc.c_str());
	auto build = setup_build(*target, *cfg, toolchains);
	cull_build(build.first);
	return execute_build(*target, build.first, *cfg, build.second);
}