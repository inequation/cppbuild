#pragma once

#include "../cbl.h"

#include <cstdio>

namespace cbl
{
	namespace detail
	{
		extern FILE *log_file_stream;
		extern FILE *trace_file_stream;

		void rotate_traces(bool append_to_current);
		void rotate_logs(bool append_to_current);

		class background_delete : public enki::ITaskSet
		{
			class worker : public enki::ITaskSet
			{
				const string_vector &list;
			public:
				worker(const string_vector &list_);

				virtual void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
			};

			std::string log_dir;

		public:
			background_delete();

			virtual void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
		};
	};
};
