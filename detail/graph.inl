#pragma once

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

#include <mutex>

namespace graph
{
	namespace detail
	{
		using cache_map_key = std::pair<std::string, platform>;
	}
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
			return combine_hash(string_hasher(k.first), byte_hasher(static_cast<uint8_t>(k.second)));
		}
	};
}

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
			uint64_t self_timestamp = action->get_oldest_output_timestamp();
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
							cbl::log_verbose("Culling input type %d %s for action %s", input->type, input->outputs[0].c_str(), action->outputs[0].c_str());
							action->inputs.erase(action->inputs.begin() + i);
						}
						else
						{
							// Keep own timestamp up to date with includes.
							self_timestamp = (self_timestamp == 0 || include_timestamp == 0) ? 0 : std::max(self_timestamp, include_timestamp);
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
				action->output_timestamps[0] = self_timestamp;
				break;
			case cpp_action::compile:
			case cpp_action::link:
				for (int i = action->inputs.size() - 1; i >= 0; --i)
				{
					auto& input = action->inputs[i];
					cull_action(input, root_timestamp);
					uint64_t input_timestamp = input ? input->get_oldest_output_timestamp() : ~0u;
					// Only cull inputs if we exist.
					if (self_timestamp > 0 && input_timestamp > 0 && (!input || input_timestamp < root_timestamp))
					{
						if (input)
						cbl::log_verbose("Culling input type %d %s for action %s", input->type, input->outputs[0].c_str(), action->outputs[0].c_str());
						action->inputs.erase(action->inputs.begin() + i);
					}
					else
					{
						// Keep own timestamp up to date with includes.
						self_timestamp = (self_timestamp == 0 || input_timestamp == 0) ? 0 : std::max(self_timestamp, input_timestamp);
					}
				}
				if (action->inputs.empty() && root_timestamp != 0)
				{
					cbl::log_verbose("Culling action type %d %s", action->type, action->outputs[0].c_str());
					action = nullptr;
				}
				else
				{
					action->output_timestamps[0] = self_timestamp;
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
		if (root->inputs.empty())
		{
			// This action is built and is probably a dependency up the tree.
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
					assert(i->type == (action::action_type)cpp_action::compile);
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
				assert(i->type == (action::action_type)cpp_action::source);
				// FIXME: Find a more appropriate place for this mkdir.
				cbl::fs::mkdir(cbl::path::get_directory(root->outputs[0].c_str()).c_str(), true);
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

	void clean_build_graph_outputs(std::shared_ptr<action> root)
	{
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
								key = key_it->first;
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
												cbl::log_verbose("[CacheSer] Failed to serialize value time stamp at index %d, key %s", i, key.c_str());
										}
										else
											cbl::log_verbose("[CacheSer] Failed to serialize value string at index %d, key %s", i, key.c_str());
									}
								}
								else
									cbl::log_verbose("[CacheSer] Failed to serialize value vector length for key %s", key.c_str());
							}
							else
								cbl::log_verbose("[CacheSer] Failed to serialize key string");
							if (key_count-- <= 1)
								break;
						} while (success);
					}
					else
						cbl::log_verbose("[CacheSer] Failed to read cache key count");
				}
				else
					cbl::log_verbose("[CacheSer] Version number mismatch (expected %d, got %d)", cache_version, v);
			}
			else
				cbl::log_verbose("[CacheSer] Magic number mismatch (expected %08X, got %08X)", cache_magic.i, m.i);
		}

		std::string get_cache_path(const target &target, const configuration& cfg)
		{
			using namespace cbl;
			using namespace cbl::path;
			return join(get_cppbuild_cache_path(), join(get_platform_str(cfg.second.platform), join(target.first, "timestamps.bin")));
		}

		static std::unordered_map<cache_map_key, timestamp_cache> cache_map;

		timestamp_cache& find_or_create_cache(const target &target, const configuration& cfg)
		{
			using namespace cbl;

			static std::mutex mutex;
			std::lock_guard<std::mutex> _(mutex);

			auto key = std::make_pair(target.first, cfg.second.platform);
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
	};

	void save_timestamp_cache(const target &target, const configuration& cfg)
	{
		using namespace cbl;

		auto& cache = detail::find_or_create_cache(target, cfg);
		std::string cache_path = detail::get_cache_path(target, cfg);
		fs::mkdir(path::get_directory(cache_path.c_str()).c_str(), true);
		if (FILE *serialized = fopen(cache_path.c_str(), "wb"))
		{
			detail::serialize_cache_items<detail::fwrite_wrapper, nullptr>(cache, serialized);
			fclose(serialized);
		}
		else
		{
			log_verbose("Failed to open timestamp cache for writing to %s", cache_path.c_str());
		}
	}

	bool query_dependency_cache(const target &target, const configuration& cfg, const std::string& source, std::function<void(const std::string &)> push_dep)
	{
		auto& cache = detail::find_or_create_cache(target, cfg);

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
					cbl::log_verbose("Outdated time stamp for dependency %s (%" PRId64 " vs %" PRId64 ")", entry.first.c_str(), stamp, entry.second);
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
		auto& cache = detail::find_or_create_cache(target, cfg);

		cache[source] = deps;
	}
};
