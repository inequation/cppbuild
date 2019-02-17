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

// Options with nullptr 'desc' will not be listed in the usage message.
option help =
	{ option::boolean,	'h',"help",			{ false },		"Print this help message and exit." };
option version =
	{ option::boolean,	'v',"version",		{ false },		"Print version number and exit." };
option log_level =
	{ option::int32,	'l',"log-level",	{ int32_t(/*cbl::severity::info*/2) },	"Set logging verbosity level. Lower level is more verbose.", option::arg_required };
option jobs =
	{ option::int32,	'j',"jobs",			{ int32_t(0) },	"Allow N jobs at once; N is hardware thread count by default.", option::arg_optional };
option dump_builds =
	{ option::boolean,	'B',"dump-builds",	{ false },		"Dump the build descriptions, as compiled from user's build.cpp." };
option dump_graph =
	{ option::int32,	'G',"dump-graph",	{ int32_t(0) },	"Dump the build graph. Argument controls the verbosity level: 0 is disabled; 1 only prints the culled graph; 2 also prints the graph before culling.", option::arg_optional };
option rotate_log_count =
	{ option::int64,	'R',"rotate-log-count",	{ 10 },		"Number of old logs to keep.", option::arg_required };
option fatal_errors =
	{ option::boolean,	'f',"fatal-errors",	{ false },		"Stop the build immediately upon first error." };

// Internal options, not meant to be exposed to user.
option append_logs =
	{ option::boolean, 0,"append-logs",		{ false },		nullptr };
option bootstrap_deploy =
	{ option::str_ptr, 0,"bootstrap-deploy",{ false },		nullptr, option::arg_required };
