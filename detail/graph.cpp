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

#include "../cppbuild.h"
#include "../cbl.h"
#include "detail.h"

#include <mutex>
#include <sstream>

using namespace graph;

using cache_map_key = std::pair<target, configuration>;
using timestamp_cache_key = std::pair<std::string, std::string>;

bool operator==(const cache_map_key &a, const cache_map_key &b)
{
	return a.first.first == b.first.first && a.second.first == b.second.first;
}

namespace std
{
	template <> struct hash<cache_map_key>
	{
		size_t operator()(const cache_map_key &k) const
		{
			using namespace cbl;
			hash<string> string_hasher;
			hash<uint8_t> byte_hasher;
			return combine_hash(string_hasher(k.first.first), byte_hasher(static_cast<uint8_t>(k.second.second.platform)));
		}
	};

	template <> struct hash<timestamp_cache_key>
	{
		size_t operator()(const timestamp_cache_key &k) const
		{
			using namespace cbl;
			hash<string> string_hasher;
			return combine_hash(string_hasher(k.first), string_hasher(k.second));
		}
	};
}

using timestamp_cache = std::unordered_map<timestamp_cache_key, dependency_timestamp_vector>;
using timestamp_cache_entry = timestamp_cache::value_type;

template <typename T>
static inline void atomic_max(std::atomic<T>& max, T const& value) noexcept
{
	T prev_value = max;
	while (prev_value < value && !max.compare_exchange_weak(prev_value, value));
}

static inline void cull_input(cull_context &ctx, graph::action &action, std::shared_ptr<graph::action> &input, uint64_t stamp_if_missing = 0)
{
	MTR_SCOPE_FUNC_S("input->outputs[0]", input && input->outputs.size() > 0 ? cbl::jsonify(input->outputs[0].c_str()).c_str() : "already culled");
	uint64_t input_timestamp = input ? input->get_oldest_output_timestamp() : stamp_if_missing;
	
	const bool input_exists = input_timestamp > 0;
	const bool older_than_graph_root = !input || input_timestamp < ctx.root_timestamp;
	const bool output_exists = ctx.self_timestamp > 0;

	// Only cull inputs if we exist.
	if (input_exists && older_than_graph_root && output_exists)
	{
		if (input)
			cbl::log_debug("Culling INPUT type %d %s for action %s (self stamp %" PRId64 ", input stamp %" PRId64 ", root stamp %" PRId64 ")",
				input->type, input->outputs[0].c_str(), action.outputs[0].c_str(), ctx.self_timestamp.load(), input_timestamp, ctx.root_timestamp);
		input = nullptr;
	}
	else
	{
		cbl::log_debug("Bumping self timestamp from input type %d %s for action %s (self stamp %" PRId64 ", input stamp %" PRId64 ", root stamp %" PRId64 ")",
			input->type, input->outputs[0].c_str(), action.outputs[0].c_str(), ctx.self_timestamp.load(), input_timestamp, ctx.root_timestamp);
		// Keep own timestamp up to date with inputs.
		if (input_timestamp == 0 || ctx.self_timestamp == 0)
			ctx.self_timestamp = 0;
		else
			atomic_max(ctx.self_timestamp, input_timestamp);
	}
};

static void prune_inputs(graph::action_vector &inputs)
{
	MTR_SCOPE_FUNC();
	for (int i = inputs.size() - 1; i >= 0; --i)
	{
		if (nullptr == inputs[i])
			inputs.erase(inputs.begin() + i);
	}
	inputs.shrink_to_fit();
};

template<bool is_linking>
static bool internal_cull_cpp_action(build_context &bctx, cull_context &ictx, cpp_action &action)
{
	/*uint64_t root_timestamp = action.get_oldest_output_timestamp();
	cull_context ictx{ root_timestamp, root_timestamp };*/

	const auto rf_path = action.response_file.c_str();
	const auto rf_timestamp = cbl::fs::get_modification_timestamp(rf_path);
	if (ictx.self_timestamp < rf_timestamp)
	{
		// Response file is newer, which means that compilation flags have changed.
		cbl::log_debug("Response file newer than product for ACTION type %d %s (%d inputs remaining; self=%" PRIu64 ", rf=%" PRIu64 ")", action.type, action.outputs[0].c_str(), action.inputs.size(), ictx.self_timestamp.load(), rf_timestamp);
		ictx.self_timestamp = rf_timestamp;
	}
	else
	{
		decltype(action.inputs) backup_inputs;
		if (is_linking)
		{
			// For linking, if any input remains at all, we need to link all of them.
			backup_inputs.insert(backup_inputs.begin(), action.inputs.begin(), action.inputs.end());
		}

		// Breadth-first parallel culling of dependencies.
		cbl::parallel_for([&](uint32_t i)
		{
			auto& input = action.inputs[i];
			cull_action(bctx, input, ictx.root_timestamp);
			cull_input(ictx, action, input, ~0u);
		},
			action.inputs.size(), 1);
		prune_inputs(action.inputs);

		if (is_linking && !action.inputs.empty() && action.inputs.size() < backup_inputs.size())
		{
			for (auto &bi : backup_inputs)
			{
				auto predicate = [&bi](const std::shared_ptr<graph::action> &in)
				{
					if (in->outputs.size() == bi->outputs.size())
					{
						for (size_t i = 0; i < in->outputs.size(); ++i)
						{
							// We can test for identity because the backup is a clone.
							if (in->outputs[i] != bi->outputs[i])
								return false;
						}
						return true;
					}
					return false;
				};
				if (action.inputs.end() == std::find_if(action.inputs.begin(), action.inputs.end(), predicate))
				{
					// We don't need to build the object, but we need to consume it.
					bi->inputs.clear();
					action.inputs.push_back(bi);
				}
			}
		}
	}

	if (action.inputs.empty() && ictx.root_timestamp != 0)
	{
		cbl::log_debug("Culling ACTION type %d %s (%d inputs remaining)", action.type, action.outputs[0].c_str(), action.inputs.size());
		return true;
	}
	else
	{
		action.output_timestamps[0] = ictx.self_timestamp;
		return false;
	}
}

static int internal_exec_cpp_action(cbl::deferred_process process, const action &action)
{
	if (process)
	{
		std::string outputs = cbl::jsonify(cbl::join(action.outputs, " "));
		cbl::info("%s", ("Building " + outputs).c_str());
		MTR_SCOPE_FUNC_S("outputs", outputs.c_str());
		if (auto spawned = process())
		{
			int exit_code = spawned->wait();
			if (exit_code != 0 && g_options.fatal_errors.val.as_bool)
				cbl::fatal(exit_code, "Building %s failed with code %d", outputs.c_str(), exit_code);
			return exit_code;
		}
	}
	return (int)error_code::failed_launching_compiler_process;
}

static bool cull_test_link(build_context &context, cull_context &ictx, action &action)
{
	return internal_cull_cpp_action<true>(context, ictx, static_cast<cpp_action &>(action));
}

static int exec_link(build_context &context, const action &action)
{
	const auto& as_cpp_action = static_cast<const cpp_action&>(action);
	MTR_SCOPE_FUNC_S("response_file", cbl::jsonify(as_cpp_action.response_file.c_str()).c_str());

	return internal_exec_cpp_action(context.tc.schedule_linker(context, as_cpp_action.response_file.c_str()), action);
}

static bool cull_test_compile(build_context &context, cull_context &ictx, action &action)
{
	return internal_cull_cpp_action<false>(context, ictx, static_cast<cpp_action &>(action));
}

static int exec_compile(build_context &context, const action &action)
{
	const auto& as_cpp_action = static_cast<const cpp_action&>(action);
	MTR_SCOPE_FUNC_S("response_file", cbl::jsonify(as_cpp_action.response_file.c_str()).c_str());

	if (action.inputs.size() == 0)
	{
		assert(0 != action.get_oldest_output_timestamp() && "No inputs and the output does not exist");
		// We are now a dummy action that only exists to gather preexisting objects for linking.
		return 0;
	}

	assert(action.outputs.size() == 1);
	auto i = action.inputs[0];
	assert(i->outputs.size() == 1);
	assert(i->type == (action::action_type)cpp_action::source);
	
	// FIXME: Find a more appropriate place for this mkdir.
	cbl::fs::mkdir(cbl::path::get_directory(action.outputs[0].c_str()).c_str(), true);

	return internal_exec_cpp_action(
		context.tc.schedule_compiler(context, as_cpp_action.response_file.c_str()),
		action
	);
}

static bool cull_test_source(build_context& context, cull_context &ictx, action& action)
{
	cbl::parallel_for([&](uint32_t i)
		{
			uint64_t start = cbl::time::now();
			auto& input = action.inputs[i];
			assert(input->type == cpp_action::include && !!"Source actions may only have includes as input");
			cull_input(ictx, action, input);
		},
		action.inputs.size(), 100);
	prune_inputs(action.inputs);
	action.output_timestamps[0] = ictx.self_timestamp;
	// Final decision is made by the compile action.
	return false;
}

static bool cull_test_include(build_context& context, cull_context &ictx, action& action)
{
	assert(!"We ought to be culled by the parent");
	return false;
}

struct action_handlers
{
	action_cull_test_handler cull;
	action_execute_handler exec;
};
static std::vector<action_handlers> g_action_handlers =
{
	action_handlers{ cull_test_link, exec_link },
	action_handlers{ cull_test_compile, exec_compile },
	action_handlers{ cull_test_source, nullptr },
	action_handlers{ cull_test_include, nullptr }
};

using task_set_ptr = std::shared_ptr<enki::ITaskSet>;

task_set_ptr enqueue_build_tasks(build_context& ctx, std::shared_ptr<graph::action> root);

class action_exec_task : public enki::ITaskSet
{
protected:
	build_context ctx;
	std::shared_ptr<graph::action> action;
public:
	cbl::deferred_process process;
	int exit_code = -1;

	action_exec_task(build_context &context, std::shared_ptr<graph::action> action_)
		: enki::ITaskSet(1, 1)
		, ctx(context)
		, action(action_)
	{}
			
	void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
	{
		assert((action->inputs.size() > 0 || nullptr != g_action_handlers[action->type].exec) && "Nothing to do for this action");
		assert(range.end - range.start == 1 && "Cannot handle ranges yet");

		std::string outputs = cbl::jsonify(cbl::join(action->outputs, " "));

		exit_code = dispatch_subtasks_and_wait(outputs.c_str());
		if (exit_code == 0)
		{
			cbl::info("%s", ("Building " + outputs).c_str());
			MTR_SCOPE_FUNC_S("outputs", outputs.c_str());

			exit_code = g_action_handlers[action->type].exec(ctx, *action);
			if (exit_code != 0 && g_options.fatal_errors.val.as_bool)
				cbl::fatal(exit_code, "Building %s failed with code %d", outputs.c_str(), exit_code);
		}
	}

protected:
	int dispatch_subtasks_and_wait(const char *outputs)
	{
		MTR_SCOPE_FUNC_S("dependents", outputs);
		// FIXME: Creating a ton of task sets is excessive, we should create one task set and feed it arrays instead.
		std::vector<task_set_ptr> subtasks;
		subtasks.reserve(action->inputs.size());
		for (auto i : action->inputs)
		{
			if (auto subtask = enqueue_build_tasks(ctx, i))
				subtasks.push_back(subtask);
		}
		// Issue the subtasks.
		for (auto& t : subtasks)
			cbl::scheduler.AddTaskSetToPipe(t.get());
		// Wait for them to complete.
		int dep_exit_code = 0;
		for (auto& t : subtasks)
		{
			cbl::scheduler.WaitforTask(t.get());
			// Propagate the first non-success exit code.
			if (dep_exit_code == 0)
				dep_exit_code = std::static_pointer_cast<action_exec_task>(t)->exit_code;
		}
		return dep_exit_code;
	}
};

static void cull_action(build_context& bctx, std::shared_ptr<graph::action>& action, uint64_t root_timestamp)
{
	static const char* types[] =
	{
		"Cull: link",
		"Cull: compile",
		"Cull: source",
		"Cull: include"
	};
	static_assert(sizeof(types) / sizeof(types[0]) == action::cpp_actions_end, "Missing string for action type");
	MTR_SCOPE_S(__FILE__,
		action->type <= cpp_action::include ? types[action->type] : "Cull: action",
		"outputs[0]",
		cbl::jsonify(action->outputs[0].c_str()).c_str());
	cull_context ictx{ action->get_oldest_output_timestamp(), root_timestamp };
	if (g_action_handlers[action->type].cull && g_action_handlers[action->type].cull(bctx, ictx, *action))
		action = nullptr;
}

task_set_ptr enqueue_build_tasks(build_context& ctx, std::shared_ptr<graph::action> root)
{
	MTR_SCOPE_FUNC();
	if (!root)	// Empty graph, nothing to build.
		return nullptr;
	if (nullptr == g_action_handlers[root->type].exec)
		return nullptr;
	return std::make_shared<action_exec_task>(ctx, root);
}

union magic
{
	char c[4];
	uint32_t i;
};

static constexpr magic cache_magic = { 'C', 'B', 'T', 'C' };
// Increment this counter every time the cache binary format changes. 
static constexpr uint32_t cache_version = 2;

typedef size_t(*serializer)(void *ptr, size_t size, size_t nmemb, FILE *stream);

static inline size_t fwrite_wrapper(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	return fwrite(const_cast<void*>(ptr), size, nmemb, stream);
};

template <serializer serializer, void(* on_success)(const timestamp_cache_key &key, const char *path, uint64_t stamp) = nullptr>
static void serialize_cache_items(timestamp_cache& cache, FILE *stream)
{
	MTR_SCOPE_FUNC();
	magic m = cache_magic;
	std::hash<std::string> hasher;
	if (1 == serializer(&m, sizeof(m), 1, stream) && m.i == cache_magic.i)
	{
		const uint64_t expected_version = ((uint64_t)cache_version << 32) | (uint64_t)cbl::get_host_platform();
		uint64_t v = expected_version;
		if (1 == serializer(&v, sizeof(v), 1, stream) && v == expected_version)
		{
			uint32_t length;
			auto key_it = cache.begin();
			std::string str;
			timestamp_cache_key key;
			uint64_t stamp;

			auto serialize_str = [stream](std::string& str) -> bool
			{
				uint32_t length = str.length();
				bool success = (1 == serializer(&length, sizeof(length), 1, stream));
				if (success)
				{
					str.resize(length);
					success = length == serializer(const_cast<char *>(str.data()), 1, length, stream);
					if (success)
					{
						str.push_back(0);
						str.resize(length);
					}
				}
				return success;
			};

			size_t key_count = cache.size();
			if (1 == serializer(&key_count, sizeof(key_count), 1, stream))
			{
				bool success;
				do
				{
					if (key_it != cache.end())
					{
						key = key_it++->first;
					}
					success = serialize_str(key.first) && serialize_str(key.second);
					if (success)
					{
						auto& vec = cache[key];
						length = vec.size();
						success = 1 == serializer(&length, sizeof(length), 1, stream);
						if (success)
						{
							vec.resize(length);
							for (uint32_t i = 0; success && i < length; ++i)
							{
								success = serialize_str(vec[i].first);
								if (success)
								{
									success = 1 == serializer(&vec[i].second, sizeof(vec[i].second), 1, stream);
									if (success)
									{
										if (on_success)
											on_success(key, vec[i].first.c_str(), vec[i].second);
									}
									else
										cbl::log_debug("[CacheSer] Failed to serialize value time stamp at index %d, key %s@%x", i, key.first.c_str(), hasher(key.second));
								}
								else
									cbl::log_debug("[CacheSer] Failed to serialize value string at index %d, key %s@%x", i, key.first.c_str(), hasher(key.second));
							}
						}
						else
							cbl::log_debug("[CacheSer] Failed to serialize value vector length for key %s@%x", key.first.c_str(), hasher(key.second));
					}
					else
						cbl::log_debug("[CacheSer] Failed to serialize key string");
					if (key_count-- <= 1)
						break;
				} while (success);
			}
			else
				cbl::log_debug("[CacheSer] Failed to read cache key count");
		}
		else
			cbl::log_debug("[CacheSer] Version number mismatch (expected %d, got %d)", cache_version, v);
	}
	else
		cbl::log_debug("[CacheSer] Magic number mismatch (expected %08X, got %08X)", cache_magic.i, m.i);
}

static std::string get_cache_path(const target &target, const configuration& cfg)
{
	using namespace cbl;
	using namespace cbl::path;
	return join(get_cppbuild_cache_path(), join(get_platform_str(cfg.second.platform), join(target.first, "timestamps.bin")));
}

static std::unordered_map<cache_map_key, timestamp_cache> cache_map;
static std::mutex cache_mutex;

static timestamp_cache& find_or_create_cache(const target &target, const configuration& cfg)
{
	MTR_SCOPE_FUNC();
	using namespace cbl;

	MTR_BEGIN(__FILE__, "find_or_create mutex");
	std::lock_guard<std::mutex> _(cache_mutex);
	MTR_END(__FILE__, "find_or_create mutex");

	auto key = std::make_pair(target, cfg);
	auto it = cache_map.find(key);
	if (it == cache_map.end())
	{
		timestamp_cache& cache = cache_map[key];
		std::string cache_path = get_cache_path(target, cfg);
		if (FILE *serialized = fopen(cache_path.c_str(), "rb"))
		{
			serialize_cache_items<fread, nullptr>(cache, serialized);
			fclose(serialized);
		}
		else
		{
			log_verbose("Failed to open timestamp cache for reading from %s, using a blank slate", cache_path.c_str());
		}
		return cache;
	}
	return it->second;
}

static void for_each_cache(std::function<void(const cache_map_key &, timestamp_cache &)> callback)
{
	std::lock_guard<std::mutex> _(cache_mutex);
	for (auto &pair : cache_map)
	{
		callback(pair.first, pair.second);
	}
}

static void dump_action(std::ostringstream& dump, std::shared_ptr<graph::action> action, size_t indent)
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

	const char* types[] =
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

namespace graph
{
	std::shared_ptr<graph::action> generate_cpp_build_graph(build_context &ctx)
	{
		MTR_SCOPE_FUNC();
		// Presize the array for safe parallel writes to it.
		decltype(action::inputs) objects;
		auto sources = ctx.trg.second.enumerate_sources();
		objects.resize(sources.size());
		cbl::parallel_for([&](uint32_t i)
			{
				std::string safe_source = cbl::jsonify(sources[i].c_str());
				MTR_SCOPE_S(__FILE__, "Generating compile action", "source", safe_source.c_str());
				objects[i] = ctx.tc.generate_compile_action_for_cpptu(ctx, sources[i].c_str());
			},
			sources.size());
		auto root = ctx.tc.generate_link_action_for_objects(ctx, objects);
		if (ctx.trg.second.generate_graph_hook)
			ctx.trg.second.generate_graph_hook(root);
		return root;
	}

	std::shared_ptr<graph::action> clone_build_graph(std::shared_ptr<graph::action> source)
	{
		MTR_SCOPE_FUNC();
		if (!source)
			return nullptr;
		return source->clone();
	}
	
	bool test_graphs_equivalent(std::shared_ptr<graph::action> a, std::shared_ptr<graph::action> b)
	{
		MTR_SCOPE_FUNC();
		if (!a != !b)
			return false;
		else if (!a)	// The above implies !b.
			return true;
		else
			return (*a) == (*b);
	}
	
	void cull_build_graph(build_context &ctx,
		std::shared_ptr<graph::action>& root)
	{
		MTR_SCOPE_FUNC();
		cull_action(ctx, root, root->get_oldest_output_timestamp());
		if (ctx.trg.second.cull_graph_hook && ctx.trg.second.cull_graph_hook(root))
			cull_build_graph(ctx, root);
	}

	int execute_build_graph(build_context &ctx,
		std::shared_ptr<graph::action> root)
	{
		MTR_SCOPE_FUNC();
		int exit_code = 0;
		if (auto root_task = enqueue_build_tasks(ctx, root))
		{
			cbl::scheduler.AddTaskSetToPipe(root_task.get());
			cbl::scheduler.WaitforTask(root_task.get());
			exit_code = std::static_pointer_cast<action_exec_task>(root_task)->exit_code;
		}
		return exit_code;
	}

	void clean_build_graph_outputs(build_context &ctx, 
		std::shared_ptr<graph::action> root)
	{
		MTR_SCOPE_FUNC();
		if (!root)
		{
			// Empty graph, nothing to clean.
			return;
		}
		if (root->type != (action::action_type)cpp_action::include)
		{
			for (auto& o : root->outputs)
			{
				cbl::fs::delete_file(o.c_str());
			}
			for (auto& i : root->inputs)
			{
				clean_build_graph_outputs(ctx, i);
			}
		}
	}

	void dump_build_graph(std::ostringstream& dump, std::shared_ptr<graph::action> root)
	{
		dump_action(dump, root, 0);
	}

	void register_action_handlers(action::action_type t,
		action_cull_test_handler cull_test,
		action_execute_handler exec)
	{
		cbl::log_debug("Registering handlers for type %d: cull_test = 0x%p, exec = 0x%p", t, cull_test, exec);
		if (t >= g_action_handlers.size())
		{
			if (t - g_action_handlers.size() > 1)
				cbl::log_debug("Growing the handler vector by more than 1, this will insert nullptr handlers for type range [%d, %d]", g_action_handlers.size(), t - 1);
			g_action_handlers.resize(t + 1, { nullptr, nullptr });
		}
		g_action_handlers[t].cull = cull_test;
		g_action_handlers[t].exec = exec;
	}

	bool operator==(const action_vector& a, const action_vector& b)
	{
		if (a.size() != b.size())
			return false;
		for (auto& i : a)
		{
			auto predicate = [&i](const std::shared_ptr<action> &j)
			{
				if (!i != !j)
					return false;
				else if (!i)	// The above implies !j.
					return true;
				else
					return (*i) == (*j);
			};
			if (b.end() == std::find_if(b.begin(), b.end(), predicate))
				return false;
		}
		return true;
	}

	void action::update_output_timestamps() const
	{
		output_timestamps.clear();
		output_timestamps.reserve(outputs.size());
		for (auto& o : outputs)
		{
			output_timestamps.push_back(cbl::fs::get_modification_timestamp(o.c_str()));
		}
	}

	uint64_t action::get_oldest_output_timestamp() const
	{
		if (output_timestamps.empty())
		{
			update_output_timestamps();
		}
		auto it = std::min_element(output_timestamps.begin(), output_timestamps.end());
		return it != output_timestamps.end() ? *it : 0;
	}

	std::shared_ptr<action> action::clone() const
	{
		auto result = internal_clone();
		result->type = type;
		result->inputs.reserve(inputs.size());
		for (auto& input : inputs)
		{
			result->inputs.push_back(input->clone());
		}
		result->outputs.insert(result->outputs.begin(), outputs.begin(), outputs.end());
		result->output_timestamps.insert(result->output_timestamps.begin(), output_timestamps.begin(), output_timestamps.end());
		return result;
	}

	bool action::operator==(action& other) const
	{
		if (other.type != type)
			return false;
		if (other.inputs != inputs)
			return false;
		if (other.outputs != outputs)
			return false;
		return internal_is_equivalent(other);
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

	std::shared_ptr<action> cpp_action::internal_clone() const
	{
		auto result = std::make_shared<cpp_action>();
		result->response_file = response_file;
		return result;
	}

	bool cpp_action::internal_is_equivalent(action &other) const
	{
		return static_cast<cpp_action&>(other).response_file == response_file;
	}

	void save_timestamp_caches()
	{
		MTR_SCOPE_FUNC();

		using namespace cbl;

		for_each_cache([](const cache_map_key &key, timestamp_cache &cache)
		{
			std::string cache_path = get_cache_path(key.first, key.second);
			fs::mkdir(path::get_directory(cache_path.c_str()).c_str(), true);
			MTR_BEGIN(__FILE__, "fopen");
			FILE *serialized = fopen(cache_path.c_str(), "wb");
			MTR_END(__FILE__, "fopen");
			if (serialized)
			{
				serialize_cache_items<fwrite_wrapper, nullptr>(cache, serialized);
				MTR_SCOPE(__FILE__, "fclose");
				fclose(serialized);
			}
			else
			{
				log_verbose("Failed to open timestamp cache for writing to %s", cache_path.c_str());
			}
		});
	}

	bool query_dependency_cache(build_context &ctx,
		const std::string& source,
		const char *response,
		std::function<void(const std::string &)> push_dep)
	{
		MTR_SCOPE_FUNC();

		auto& cache = find_or_create_cache(ctx.trg, ctx.cfg);
		
		const auto key = timestamp_cache_key{ source, response };
		auto it = cache.find(key);
		if (it != cache.end())
		{
			bool up_to_date = true;
			cbl::parallel_for(
				[&](uint32_t i)
			{
				const auto &entry = *(it->second.begin() + i);
				uint64_t stamp = cbl::fs::get_modification_timestamp(entry.first.c_str());
				if (stamp == 0 || stamp != entry.second)
				{
					cbl::log_verbose("Outdated time stamp for dependency %s (%" PRId64 " vs %" PRId64 ") of %s", entry.first.c_str(), stamp, entry.second, source.c_str());
					up_to_date = false;
				}
			},
				it->second.size(),
				100
			);
			if (up_to_date)
			{
				for (const auto &entry : it->second)
				{
					push_dep(entry.first);
				}
				cbl::log_verbose("Timestamp cache HIT for TU %s", source.c_str());
				return true;
			}
			else
			{
				cache.erase(it);
				cbl::log_verbose("Timestamp cache STALE for TU %s, discarded", source.c_str());
				return false;
			}
		}
		cbl::log_verbose("Timestamp cache MISS for TU %s", source.c_str());
		return false;
	}

	void insert_dependency_cache(build_context &ctx,
		const std::string& source,
		const char *response,
		const dependency_timestamp_vector &deps)
	{
		MTR_SCOPE_FUNC();

		auto& cache = find_or_create_cache(ctx.trg, ctx.cfg);

		const auto key = timestamp_cache_key{ source, response };
		cache[key] = deps;
	}
};
