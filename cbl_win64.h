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

namespace cbl
{
	namespace win64
	{
		namespace registry
		{
			enum class hkey : uint8_t
			{
				classes_root,
				current_config,
				current_user,
				local_machine,
				users
			};
			
			enum class type : uint8_t
			{
				binary_blob,
				dword,
				string,
				multiple_strings,
				qword
			};

			bool read_key(hkey root_key, const char *sub_key, const char *value_name, void *in_buffer, size_t& in_out_size, const type& expected_type);
			// Attempts to read all combinations of HK[CR|LM]\Software[\WoW6432Node]\<sub_key>.
			bool try_read_software_key(const char *sub_key, const char *value_name, void *in_buffer, size_t& in_out_size, const type& expected_type);
			bool try_read_software_path_key(const char *sub_key, const char *value_name, std::string &in_out_path);
		}

		namespace debug
		{
			std::string get_pdb_path_for_module(uintptr_t base_pointer);
			void filter_own_pdb(string_vector& paths);
		}

		bool wide_str_to_utf8_str(std::string& utf8, wchar_t *wide);
	}
}