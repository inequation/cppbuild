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

#pragma once

#include "../cppbuild.h"

enum class error_code : int
{
	success = 0,

	// This is high enough to avoid collisions with system error codes (Windows in particular; see https://docs.microsoft.com/en-us/windows/desktop/debug/system-error-codes).
	first_cppbuild_code = 16000,

	failed_bootstrap_build = first_cppbuild_code,
	failed_bootstrap_bad_toolchain,
	failed_bootstrap_deployment,
	failed_bootstrap_respawn,

	unknown_target,
	unknown_configuration,

	failed_writing_response_file,
};

namespace cppbuild
{
	struct option
	{
		// Only the first member of a union gets initialized by aggregate init. Thus the largest goes first.
		union value
		{
			int64_t as_int64;
			int32_t as_int32;
			const char *as_str_ptr;
			bool as_bool;

			bool operator==(const value &other) { return as_int64 == other.as_int64; }
		};

		// Constant "description" part.
		const enum option_type
		{
			int64,
			int32,
			str_ptr,	// String data lifetime needs to be at least as long as the process' itself.
			boolean
		} type;
		const char short_opt = 0;
		const char * const long_opt = nullptr;
		const value default_val = { int64_t(0) };
		
		const char * const desc = nullptr;
		const enum
		{
			arg_none,
			arg_optional,
			arg_required
		} arg = arg_none;

		// Mutable value.
		value val = default_val;
	};

	struct option_definitions
	{
		#include "option_definitions.inl"
	};

	struct options : public option_definitions
	{
		option *begin() const { return (option *)(this); }
		option *end() const { return (option *)(&terminator); }
		size_t size() const { return end() - begin(); }
		option &operator[](size_t index) const { assert(index < size()); return begin()[index]; }

		enum { count = sizeof(cppbuild::option_definitions) / sizeof(cppbuild::option) };

	private:
		const option terminator = { (option::option_type)0, 0 };
	};

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

extern cppbuild::options g_options;
extern int parse_args(int argc, const char **argv);
extern void print_version();
extern void print_usage(const char *argv0);

extern void discover_toolchains(toolchain_map& toolchains);

extern void rotate_traces(bool append_to_current);
extern void rotate_logs(bool append_to_current);

extern void init_process_group();
extern void terminate_process_group(int exit_code);
