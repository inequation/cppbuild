#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

namespace graph
{
	std::shared_ptr<action> generate_cpp_build_graph(const target& target, const configuration& cfg, std::shared_ptr<toolchain> tc)
	{
		std::shared_ptr<cpp_action> root = std::make_shared<cpp_action>();
		root->type = (action::action_type)cpp_action::link;
		auto sources = target.second.sources();
		root->outputs.push_back(target.second.output);
		for (auto& tu : sources)
		{
			auto action = tc->generate_compile_action_for_cpptu(target, tu, cfg);
			root->inputs.emplace_back(action);
		}
		return std::static_pointer_cast<action, cpp_action>(root);
	}

	namespace detail
	{
		void cull_action(std::shared_ptr<action>& action, uint64_t root_timestamp)
		{
			switch (action->type)
			{
			case cpp_action::include:
				assert(!"We ought to be culled by the parent");
				return;
			case cpp_action::source:
				for (int i = action->inputs.size() - 1; i >= 0; --i)
				{
					auto& input = action->inputs[i];
					switch (input->type)
					{
					case cpp_action::include:
						{
							uint64_t include_timestamp = input->get_oldest_output_timestamp();
							if (include_timestamp < root_timestamp)
							{
								action->inputs.erase(action->inputs.begin() + i);
							}
						}
						break;
					case cpp_action::source:
						assert(!"Source actions may only have includes as input");
						break;
					default:
						assert(!"Unimplmented");
						abort();
					}
				}
				if (action->inputs.empty())
				{
					action = nullptr;
				}
				break;
			case cpp_action::compile:
			case cpp_action::link:
				for (int i = action->inputs.size() - 1; i >= 0; --i)
				{
					auto& input = action->inputs[i];
					cull_action(input, root_timestamp);
					if (!input)
					{
						action->inputs.erase(action->inputs.begin() + i);
					}
				}
				if (action->inputs.empty())
				{
					action = nullptr;
				}
				break;
			default:
				assert(!"Unimplmented");
				abort();
			}
		}
	};

	void cull_build_graph(std::shared_ptr<action>& root)
	{
		uint64_t root_timestamp = root->get_oldest_output_timestamp();
		detail::cull_action(root, root_timestamp);
	}

	int execute_build_graph(const target& target,
		std::shared_ptr<action> root,
		const configuration& cfg,
		std::shared_ptr<toolchain> tc,
		const cbl::pipe_output_callback& on_stderr,
		const cbl::pipe_output_callback& on_stdout)
	{
		if (!root)
		{
			// Empty graph, nothing to build.
			return 0;
		}
		switch (root->type)
		{
		case cpp_action::link:
			{
				for (auto i : root->inputs)
				{
					// TODO: Collect child processes instead for parallel execution.
					int exit_code = execute_build_graph(target, i, cfg, tc, on_stderr, on_stdout);
					if (exit_code != 0)
					{
						return exit_code;
					}
				}

				string_vector inputs;
				for (auto i : root->inputs)
				{
					assert(i->type == cpp_action::compile);
					inputs.insert(inputs.begin(), i->outputs.begin(), i->outputs.end());
				}
				if (auto p = tc->invoke_linker(target, inputs, cfg, on_stderr, on_stdout))
				{
					int exit_code = p->wait();
					if (exit_code != 0)
					{
						return exit_code;
					}
				}
				else
				{
					return 1;
				}
			}
			break;
		case cpp_action::compile:
			{
				assert(root->outputs.size() == 1);
				assert(root->inputs.size() == 1);
				auto i = root->inputs[0];
				assert(i->outputs.size() == 1);
				assert(i->type == cpp_action::source);
				if (auto p = tc->invoke_compiler(target, root->outputs[0], i->outputs[0], cfg, on_stderr, on_stdout))
				{
					int exit_code = p->wait();
					if (exit_code != 0)
					{
						return exit_code;
					}
				}
				else
				{
					return 1;
				}
			}
			break;
		case cpp_action::generate:
			for (auto i : root->inputs)
			{
				int exit_code = execute_build_graph(target, i, cfg, tc, on_stderr, on_stdout);
				if (exit_code != 0)
				{
					return exit_code;
				}
			}
			break;
		default:
			assert(!"Unimplmented");
			break;
		}
		return 0;
	}

	void action::update_output_timestamps()
	{
		output_timestamps.clear();
		output_timestamps.reserve(outputs.size());
		for (auto& o : outputs)
		{
			output_timestamps.push_back(cbl::fs::get_modification_timestamp(o.c_str()));
		}
	}

	uint64_t action::get_oldest_output_timestamp()
	{
		if (output_timestamps.empty())
		{
			update_output_timestamps();
		}
		auto it = std::min_element(output_timestamps.begin(), output_timestamps.end());
		return it != output_timestamps.end() ? *it : 0;
	}

	bool cpp_action::are_dependencies_met()
	{
		if (!inputs.empty())
		{
			for (auto& i : inputs)
			{
				if (!i->are_dependencies_met())
				{
					return false;
				}
			}
			return true;
		}
		else
		{
			uint64_t outputs = get_oldest_output_timestamp();

			uint64_t newest_input = 0;
			for (auto& i : inputs)
			{
				newest_input = std::max(newest_input, i->get_oldest_output_timestamp());
			}

			return outputs > newest_input;
		}
	}
};
