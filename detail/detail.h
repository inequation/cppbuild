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
		// Options with nullptr 'desc' will not be listed in the usage message.
		option help =
			{ option::boolean,	'h',"help",			{ false },		"Print this help message and exit." };
		option version =
			{ option::boolean,	'v',"version",		{ false },		"Print version number and exit." };
		option log_level =
			{ option::int32,	'l',"log-level",	{ int32_t(/*cbl::severity::info*/2) },	"Set logging verbosity level. Lower level is more verbose." };
		option jobs =
			{ option::int32,	'j',"jobs",			{ int32_t(0) },	"Allow N jobs at once; N is hardware thread count by default.", option::arg_optional };

		// Internal options, not meant to be exposed to user.
		option append_logs =
			{ option::boolean, 0,"append-logs",		{ false },		nullptr };
		option bootstrap_deploy =
			{ option::str_ptr, 0,"bootstrap-deploy",{ false },		nullptr, option::arg_required };
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
};

extern cppbuild::options g_options;
extern int parse_args(int &argc, char **&argv);
extern void print_version();
extern void print_usage(const char *argv0);