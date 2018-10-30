#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

void dump_builds(const target_map& t, const configuration_map& c)
{
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
			cfg.second.optimize == configuration::Os ? "s" : std::to_string((int)cfg.second.optimize).c_str());
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
	detail::dump_action(root, 0);
}

std::pair<std::shared_ptr<graph::action>, std::shared_ptr<toolchain>> setup_build(const target& target, const configuration& cfg, toolchain_map& toolchains)
{
	const char *used_tc = target.second.used_toolchain;
	if (!used_tc)
	{
		used_tc = cbl::get_default_toolchain_for_host();
	}
	assert(toolchains.find(used_tc) != toolchains.end() && "Unknown toolchain");
	auto& tc = toolchains[used_tc];
	tc->initialize(cfg);
	return std::make_pair(
		graph::generate_cpp_build_graph(target, cfg, tc),
		tc
	);
}

void cull_build(std::shared_ptr<graph::action>& root)
{
	/*printf("Build graph before culling:\n");
	dump_graph(std::static_pointer_cast<graph::cpp_action>(root));*/

	graph::cull_build_graph(root);
	printf("Build graph after culling:\n");
	dump_graph(std::static_pointer_cast<graph::cpp_action>(root));
}

int execute_build(const target& target, std::shared_ptr<graph::action> root, const configuration& cfg, std::shared_ptr<toolchain> tc)
{
	auto on_output = [](const void *data, size_t length)
	{
		printf("%.*s", (int)length, (const char *)data);
	};
	int exit_code = graph::execute_build_graph(target, root, cfg, tc, on_output, on_output);
	printf("Build finished with code %d\n", exit_code);
	return exit_code;
}

namespace detail
{
	std::pair<target, configuration> describe_bootstrap_target(toolchain_map& toolchains)
	{
		using namespace cbl;

		target_data target;
		target.type = target_data::executable;
		target.sources = fvwrap("cppbuild.cpp");

		configuration cfg;
		cfg = base_configurations::debug(cbl::get_host_platform());
		cfg.additional_include_directories.push_back("cppbuild");
		cfg.definitions.push_back(std::make_pair("CPPBUILD_SELF_HOSTED", "1"));

		std::string cache_dir = path::join("cppbuild", "cache");
		mkdir(cache_dir.c_str(), true);

		std::string new_cppbuild = path::join(cache_dir, "cppbuild-");
		new_cppbuild += std::to_string(process::get_current_pid());
#if defined(_WIN64)
		new_cppbuild += ".exe";
#endif

		return std::make_pair(
			std::make_pair(new_cppbuild, target),
			cfg
		);
	}

	void bootstrap(toolchain_map& toolchains, int argc, char *argv[])
	{
		auto bootstrap = describe_bootstrap_target(toolchains);

		auto build = setup_build(bootstrap.first, bootstrap.second, toolchains);
#if CPPBUILD_SELF_HOSTED	// Only cull the build graph once we have successfully bootstrapped.
		cull_build(build.first);
#endif
		if (build.first)
		{
			printf("cppbuild executable is outdated, bootstrapping\n");
			if (0 == execute_build(bootstrap.first, build.first, bootstrap.second, build.second))
			{
				std::string cmdline = bootstrap.first.first;
				cmdline += " _bootstrap_deploy "
					+ std::to_string(cbl::process::get_current_pid()) + " "
					+ cbl::process::get_current_executable_path();
				// Pass in any 
				for (int i = 1; i < argc; ++i)
				{
					cmdline += ' ';
					cmdline += argv[i];
				}
				auto p = cbl::process::start_async(cmdline.c_str());
				if (!p)
				{
					printf("FATAL: Failed to bootstrap cppbuild\n");
					abort();
				}
				p->detach();
				exit(0);
			}
		}
		else
		{
			printf("cppbuild executable up to date\n");
			// TODO: Take the opportunity to delete old bootstrap executables.
		}
	}
};

int main(int argc, char *argv[])
{
	if (argc >= 4 && 0 == strcmp(argv[1], "_bootstrap_deploy"))
	{
		// Finish the bootstrap deployment: 
		uint32_t parent_pid = atoi(argv[2]);
		cbl::process::wait_for_pid(parent_pid);
		if (cbl::fs::copy_file(
			cbl::process::get_current_executable_path().c_str(),
			argv[3],
			cbl::fs::maintain_timestamps | cbl::fs::overwrite))
		{
			printf("Successful bootstrap deployment\n");
			if (argc >= 3)
			{
				std::string cmdline;
				for (int i = 3; i < argc; ++i)
				{
					cmdline += ' ';
					cmdline += argv[i];
				}
				auto p = cbl::process::start_async(cmdline.c_str());
				if (p)
				{
					p->detach();
					return 0;
				}
				else
				{
					return 1;
				}
			}
			return 0;
		}
		else
		{
			fprintf(stderr, "Failed to overwrite the cppbuild executable\n");
			return 1;
		}
	}

	target_map targets;
	configuration_map configs;
	toolchain_map toolchains;
	discover_toolchains(toolchains);

	// If we were in need of bootstrapping, this call will terminate the process.
	detail::bootstrap(toolchains, argc, argv);

	describe(targets, configs, toolchains);
	dump_builds(targets, configs);

	// TODO: Build the actual targets. :)

	return 0;
}
