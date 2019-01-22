#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#include <mutex>
#include <atomic>

namespace graph
{
	namespace detail
	{
		using cache_map_key = std::pair<target, configuration>;
	}
}
bool operator==(const graph::detail::cache_map_key &a, const graph::detail::cache_map_key &b)
{
	return a.first.first == b.first.first && a.second.first == b.second.first;
}
namespace std
{
	template <> struct hash<graph::detail::cache_map_key>
	{
		size_t operator()(const graph::detail::cache_map_key &k) const
		{
			using namespace cbl;
			hash<string> string_hasher;
			hash<uint8_t> byte_hasher;
			return combine_hash(string_hasher(k.first.first), byte_hasher(static_cast<uint8_t>(k.second.second.platform)));
		}
	};
}

namespace graph
{
	namespace detail
	{
		template <typename T>
		inline void atomic_max(std::atomic<T>& max, T const& value) noexcept
		{
			T prev_value = max;
			while (prev_value < value && !max.compare_exchange_weak(prev_value, value));
		}

		void cull_action(std::shared_ptr<action>& action, uint64_t root_timestamp)
		{
			static const char *types[] =
			{
				"cull_action_link",
				"cull_action_compile",
				"cull_action_source",
				"cull_action_include",
				"cull_action_generate"
			};
			static_assert(sizeof(types) / sizeof(types[0]) == graph::action::cpp_actions_end, "Missing string for action type");
			MTR_SCOPE("graph", action->type <= cpp_action::generate ? types[action->type] : "cull_action");
			std::atomic_uint64_t self_timestamp(action->get_oldest_output_timestamp());
			auto update_self_timestamp = [&self_timestamp](uint64_t input_timestamp)
			{
				if (input_timestamp == 0 || self_timestamp == 0)
					self_timestamp = 0;
				else
					atomic_max(self_timestamp, input_timestamp);
			};

			auto cull_input = [&](std::shared_ptr<graph::action> &input, uint64_t stamp_if_missing = 0)
			{
				MTR_SCOPE("graph", "cull_input");
				uint64_t input_timestamp = input ? input->get_oldest_output_timestamp() : stamp_if_missing;
				// Only cull inputs if we exist.
				if (input_timestamp > 0 && (!input || input_timestamp < root_timestamp) && self_timestamp > 0)
				{
					if (input)
						cbl::log_debug("Culling INPUT type %d %s for action %s (self stamp %" PRId64 ", input stamp %" PRId64 ", root stamp %" PRId64 ")",
							input->type, input->outputs[0].c_str(), action->outputs[0].c_str(), self_timestamp.load(), input_timestamp, root_timestamp);
					input = nullptr;
				}
				else
				{
					cbl::log_debug("Bumping self timestamp from input type %d %s for action %s (self stamp %" PRId64 ", input stamp %" PRId64 ", root stamp %" PRId64 ")",
						input->type, input->outputs[0].c_str(), action->outputs[0].c_str(), self_timestamp.load(), input_timestamp, root_timestamp);
					// Keep own timestamp up to date with inputs.
					if (input_timestamp == 0 || self_timestamp == 0)
						self_timestamp = 0;
					else
						atomic_max(self_timestamp, input_timestamp);
				}
			};

			auto erase_null_inputs = [](std::vector<std::shared_ptr<graph::action>> &inputs)
			{
				MTR_SCOPE("graph", "prune_inputs");
				for (int i = inputs.size() - 1; i >= 0; --i)
				{
					if (nullptr == inputs[i])
						inputs.erase(inputs.begin() + i);
				}
				inputs.shrink_to_fit();
			};

			switch (action->type)
			{
			case cpp_action::include:
				assert(!"We ought to be culled by the parent");
				return;
			case cpp_action::source:
				cbl::parallel_for([&](uint32_t i)
				{
					uint64_t start = cbl::time::now();
					auto& input = action->inputs[i];
					switch (input->type)
					{
					case cpp_action::include:
						cull_input(input);
					break;
					case cpp_action::source:
						assert(!"Source actions may only have includes as input");
						break;
					default:
						assert(!"Unimplmented");
						abort();
					}
				},
					action->inputs.size(), 100);
				erase_null_inputs(action->inputs);
				action->output_timestamps[0] = self_timestamp;
				break;
			case cpp_action::compile:
			case cpp_action::link:
			{
				decltype(action->inputs) backup_inputs;
				const bool is_linking = action->type == cpp_action::link;
				if (is_linking)
				{
					// For linking, if any input remains at all, we need to link all of them.
					backup_inputs.insert(backup_inputs.begin(), action->inputs.begin(), action->inputs.end());
				}
				cbl::parallel_for([&](uint32_t i)
					{
						uint64_t start = cbl::time::now();
						auto& input = action->inputs[i];
						cull_action(input, root_timestamp);
						cull_input(input, ~0u);
					},
					action->inputs.size(), 1);
				erase_null_inputs(action->inputs);
				if (is_linking && !action->inputs.empty() && action->inputs.size() < backup_inputs.size())
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
						if (action->inputs.end() == std::find_if(action->inputs.begin(), action->inputs.end(), predicate))
						{
							// We don't need to build the object, but we need to consume it.
							bi->inputs.clear();
							action->inputs.push_back(bi);
						}
					}
				}
				if (action->inputs.empty() && root_timestamp != 0)
				{
					cbl::log_debug("Culling ACTION type %d %s (%d inputs remaining)", action->type, action->outputs[0].c_str(), action->inputs.size());
					action = nullptr;
				}
				else
				{
					action->output_timestamps[0] = self_timestamp;
				}
				break;
			}
			default:
				assert(!"Unimplmented");
				abort();
			}
		}

		using task_set_ptr = std::shared_ptr<enki::ITaskSet>;

		struct build_context
		{
			const ::target& target;
			const configuration& cfg;
			std::shared_ptr<toolchain> tc;
			const cbl::pipe_output_callback& on_stderr;
			const cbl::pipe_output_callback& on_stdout;
		};

		task_set_ptr enqueue_build_tasks(build_context ctx, std::shared_ptr<action> root);

		class build_task : public enki::ITaskSet
		{
		protected:
			build_context ctx;
			std::shared_ptr<action> root;
		public:
			cbl::deferred_process process;
			int exit_code = -1;

			build_task(build_context &context, std::shared_ptr<action> root_action, cbl::deferred_process work = nullptr,
				uint32_t set_size = 1, uint32_t min_size_for_splitting_to_threads = 1)
				: enki::ITaskSet(set_size, min_size_for_splitting_to_threads)
				, ctx(context)
				, root(root_action)
				, process(work)
			{}
			
			void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
			{
				if (process)
				{
					std::string outputs = cbl::join(root->outputs, " ");
					// Avoid JSON escape sequence issues.
					for (auto& c : outputs) { if (c == '\\') c = '/'; }
					cbl::info("%s", ("Building " + outputs).c_str());
					MTR_SCOPE_S("build", "build_task", "outputs", outputs.c_str());
					auto spawned = process();
					if (spawned)
					{
						exit_code = spawned->wait();
						if (exit_code != 0)
						{
							// FIXME: Conditionally cancel the build.
						}
					}
				}
			}
		};

		class build_task_with_deps : public build_task
		{
		protected:
			void dispatch_subtasks_and_wait(enki::TaskSetPartition range, uint32_t threadnum)
			{
				MTR_SCOPE("build", "subtask_dispatch");
				// FIXME: Creating a ton of task sets is excessive, we should create one task set and feed it arrays instead.
				std::vector<std::shared_ptr<enki::ITaskSet>> subtasks;
				subtasks.reserve(range.end - range.start);
				for (uint32_t i = range.start; i < range.end; ++i)
				{
					if (auto subtask = enqueue_build_tasks(ctx, root->inputs[i]))
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
						dep_exit_code = std::static_pointer_cast<build_task_with_deps>(t)->exit_code;
				}
				exit_code = dep_exit_code;
			}

		public:
			build_task_with_deps(build_context &context, std::shared_ptr<action> root_action, cbl::deferred_process work = nullptr,
				uint32_t min_size_for_splitting_to_threads = 1)
				: build_task(context, root_action, work, root_action->inputs.size(), min_size_for_splitting_to_threads)
			{}

			void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
			{
				dispatch_subtasks_and_wait(range, threadnum);
				if (exit_code == 0)
					// Dependencies ran correctly, run our own stuff.
					build_task::ExecuteRange(range, threadnum);
			}
		};

		class compile_task : public build_task
		{
		public:
			compile_task(build_context& context, std::shared_ptr<action> root)
				: build_task(context, root,
					context.tc->invoke_compiler(
						context.target,
						root->outputs[0],
						root->inputs[0]->outputs[0],
						context.cfg, context.on_stderr, context.on_stdout))
			{}

			void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
			{
				MTR_SCOPE("build", "compile");

				assert(root->outputs.size() == 1);
				assert(root->inputs.size() == 1);
				auto i = root->inputs[0];
				assert(i->outputs.size() == 1);
				assert(i->type == (action::action_type)cpp_action::source);

				// FIXME: Find a more appropriate place for this mkdir.
				cbl::fs::mkdir(cbl::path::get_directory(root->outputs[0].c_str()).c_str(), true);
				
				build_task::ExecuteRange(range, threadnum);
			}
		};

		class link_task : public build_task_with_deps
		{
		public:
			link_task(build_context &context, std::shared_ptr<action> root)
				: build_task_with_deps(context, root)
			{
				string_vector inputs;
				for (auto i : root->inputs)
				{
					assert(i->type == (action::action_type)cpp_action::compile);
					inputs.insert(inputs.begin(), i->outputs.begin(), i->outputs.end());
				}
				process = ctx.tc->invoke_linker(
					ctx.target,
					inputs,
					ctx.cfg, ctx.on_stderr, ctx.on_stdout);
			}

			void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
			{
				MTR_SCOPE("build", "link");
				build_task_with_deps::ExecuteRange(range, threadnum);
			}
		};

		class generate_task : public build_task_with_deps
		{
		public:
			using build_task_with_deps::build_task_with_deps;

			void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
			{
				// Only run subtasks.
				// TODO: Revise this if we ever need to actually spawn a process here.
				dispatch_subtasks_and_wait(range, threadnum);
			}
		};

		task_set_ptr enqueue_build_tasks(build_context ctx, std::shared_ptr<action> root)
		{
			MTR_SCOPE("build", "enqueue");
			if (!root)
			{
				// Empty graph, nothing to build.
				return 0;
			}
			if (root->inputs.empty())
			{
				// This action is built and is probably a dependency up the tree.
				return 0;
			}
			switch (root->type)
			{
			case cpp_action::link:
				return std::make_shared<link_task>(ctx, root);
			case cpp_action::compile:
				return std::make_shared<compile_task>(ctx, root);
			case cpp_action::generate:
				return std::make_shared<generate_task>(ctx, root);
			default:
				assert(!"Unimplmented");
				break;
			}
			return nullptr;
		}
	};

	std::shared_ptr<action> generate_cpp_build_graph(const target& target, const configuration& cfg, std::shared_ptr<toolchain> tc)
	{
		MTR_SCOPE("graph", "graph_generation");
		std::shared_ptr<cpp_action> root = std::make_shared<cpp_action>();
		root->type = (action::action_type)cpp_action::link;
		auto sources = target.second.sources();
		root->outputs.push_back(target.second.output);
		// Presize the array for safe parallel writes to it.
		root->inputs.resize(sources.size());
		cbl::parallel_for([&](uint32_t i)
			{ root->inputs[i] = tc->generate_compile_action_for_cpptu(target, sources[i], cfg); },
			sources.size(), 100);
		return std::static_pointer_cast<action, cpp_action>(root);
	}
	
	void cull_build_graph(std::shared_ptr<action>& root)
	{
		MTR_SCOPE("graph", "graph_culling");
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
		MTR_SCOPE("graph", "graph_execution");
		detail::build_context ctx{ target, cfg, tc, on_stderr, on_stdout };
		int exit_code = 0;
		if (auto root_task = detail::enqueue_build_tasks(ctx, root))
		{
			cbl::scheduler.AddTaskSetToPipe(root_task.get());
			cbl::scheduler.WaitforTask(root_task.get());
			exit_code = std::static_pointer_cast<detail::build_task>(root_task)->exit_code;
		}
		return exit_code;
	}

	void clean_build_graph_outputs(std::shared_ptr<action> root)
	{
		MTR_SCOPE("graph", "graph_cleaning");
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
				clean_build_graph_outputs(i);
			}
		}
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

	namespace detail
	{
		union magic
		{
			char c[4];
			uint32_t i;
		};

		static constexpr magic cache_magic = { 'C', 'B', 'T', 'C' };
		// Increment this counter every time the cache binary format changes. 
		static constexpr uint32_t cache_version = 0;

		typedef size_t(*serializer)(void *ptr, size_t size, size_t nmemb, FILE *stream);

		inline size_t fwrite_wrapper(void *ptr, size_t size, size_t nmemb, FILE *stream)
		{
			return fwrite(const_cast<void*>(ptr), size, nmemb, stream);
		};

		template <serializer serializer, void(* on_success)(const char *key, const char *path, uint64_t stamp) = nullptr>
		void serialize_cache_items(timestamp_cache& cache, FILE *stream)
		{
			MTR_SCOPE("cache", "cache_serializer");
			magic m = cache_magic;
			if (1 == serializer(&m, sizeof(m), 1, stream) && m.i == cache_magic.i)
			{
				const uint64_t expected_version = ((uint64_t)cache_version << 32) | (uint64_t)cbl::get_host_platform();
				uint64_t v = expected_version;
				if (1 == serializer(&v, sizeof(v), 1, stream) && v == expected_version)
				{
					uint32_t length;
					auto key_it = cache.begin();
					std::string str, key;
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
							success = serialize_str(key);
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
													on_success(key.c_str(), vec[i].first.c_str(), vec[i].second);
											}
											else
												cbl::log_debug("[CacheSer] Failed to serialize value time stamp at index %d, key %s", i, key.c_str());
										}
										else
											cbl::log_debug("[CacheSer] Failed to serialize value string at index %d, key %s", i, key.c_str());
									}
								}
								else
									cbl::log_debug("[CacheSer] Failed to serialize value vector length for key %s", key.c_str());
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

		std::string get_cache_path(const target &target, const configuration& cfg)
		{
			using namespace cbl;
			using namespace cbl::path;
			return join(get_cppbuild_cache_path(), join(get_platform_str(cfg.second.platform), join(target.first, "timestamps.bin")));
		}

		static std::unordered_map<cache_map_key, timestamp_cache> cache_map;
		static std::mutex cache_mutex;

		timestamp_cache& find_or_create_cache(const target &target, const configuration& cfg)
		{
			MTR_SCOPE("cache", "cache_find_or_create");
			using namespace cbl;

			MTR_BEGIN("cache", "find_or_create_mutex");
			std::lock_guard<std::mutex> _(cache_mutex);
			MTR_END("cache", "find_or_create_mutex");

			auto key = std::make_pair(target, cfg);
			auto it = cache_map.find(key);
			if (it == cache_map.end())
			{
				timestamp_cache& cache = cache_map[key];
				std::string cache_path = get_cache_path(target, cfg);
				if (FILE *serialized = fopen(cache_path.c_str(), "rb"))
				{
					detail::serialize_cache_items<fread, nullptr>(cache, serialized);
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

		void for_each_cache(std::function<void(const cache_map_key &, timestamp_cache &)> callback)
		{
			std::lock_guard<std::mutex> _(cache_mutex);
			for (auto &pair : cache_map)
			{
				callback(pair.first, pair.second);
			}
		}
	};

	void save_timestamp_caches()
	{
		MTR_SCOPE("cache", "cache_save");

		using namespace cbl;

		detail::for_each_cache([](const detail::cache_map_key &key, timestamp_cache &cache)
		{
			std::string cache_path = detail::get_cache_path(key.first, key.second);
			fs::mkdir(path::get_directory(cache_path.c_str()).c_str(), true);
			MTR_BEGIN("cache", "before");
			FILE *serialized = fopen(cache_path.c_str(), "wb");
			MTR_END("cache", "before");
			if (serialized)
			{
				detail::serialize_cache_items<detail::fwrite_wrapper, nullptr>(cache, serialized);
				MTR_SCOPE("cache", "fclose");
				fclose(serialized);
			}
			else
			{
				MTR_SCOPE("cache", "log");
				log_verbose("Failed to open timestamp cache for writing to %s", cache_path.c_str());
			}
		});
	}

	bool query_dependency_cache(const target &target, const configuration& cfg, const std::string& source, std::function<void(const std::string &)> push_dep)
	{
		MTR_SCOPE("cache", "cache_query");

		auto& cache = detail::find_or_create_cache(target, cfg);

		auto it = cache.find(source);
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
					cbl::log_verbose("Outdated time stamp for dependency %s (%" PRId64 " vs %" PRId64 ")", entry.first.c_str(), stamp, entry.second);
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

	void insert_dependency_cache(const target &target, const configuration& cfg, const std::string& source, const graph::dependency_timestamp_vector &deps)
	{
		MTR_SCOPE("cache", "cache_insert");

		auto& cache = detail::find_or_create_cache(target, cfg);

		cache[source] = deps;
	}
};
