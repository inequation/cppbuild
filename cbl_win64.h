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

		bool wide_str_to_ansi_str(std::string& ansi, wchar_t *wide);
	}
}