#if defined(_WIN64)

#include "../cppbuild.h"
#include "../cbl.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <io.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace cbl
{
	namespace win64
	{
		inline std::string get_last_error_str()
		{
			DWORD error = GetLastError();
			std::string buffer(256 - 1, 0);
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				const_cast<LPSTR>(buffer.data()), buffer.size(), nullptr);
			return buffer;
		}
	}

	namespace path
	{
		bool is_path_separator(char c)
		{
			// A lot of C++ code will use forward slashes, so support that as well.
			return c == '/' || c == '\\';
		}

		std::string get_absolute(const char *path)
		{
			std::string abs;
			abs.resize(MAX_PATH);
			DWORD written = GetFullPathNameA(path, abs.size(), (LPSTR)abs.data(), nullptr);
			if (!written || written > abs.size())
				return "";
			else
			{
				abs.resize(written);
				return abs;
			}
		}

		std::string get_working_path()
		{
			std::string cwd;
			cwd.resize(MAX_PATH);
			DWORD written = GetCurrentDirectoryA(cwd.size(), (LPSTR)cwd.data());
			if (!written || written > cwd.size())
				return "";
			else
			{
				cwd.resize(written);
				return cwd;
			}
		}
	};

	namespace time
	{
		uint64_t now()
		{
			uint64_t stamp = 0;
			SYSTEMTIME st;
			FILETIME ft;
			GetSystemTime(&st);
			if (SystemTimeToFileTime(&st, &ft))
			{
				stamp = (uint64_t)ft.dwLowDateTime;
				stamp |= ((uint64_t)ft.dwHighDateTime) << 32;
			}
			return stamp;
		}

		void of_day(const uint64_t stamp, int *y, int *M, int *d, int *h, int *m, int *s, int *us)
		{
			FILETIME ft;
			constexpr uint64_t low_mask = (1ull << (sizeof(ft.dwLowDateTime) * 8)) - 1;
			constexpr uint64_t high_mask = ~low_mask;
			constexpr uint64_t high_shift = sizeof(ft.dwLowDateTime) * 8;
			ft.dwHighDateTime = (stamp & high_mask) >> high_shift;
			ft.dwLowDateTime = stamp & low_mask;
			
			SYSTEMTIME utc, local;
			FileTimeToSystemTime(&ft, &utc);
			SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);

			if (y) *y = local.wYear;
			if (M) *M = local.wMonth;
			if (d) *d = local.wDay;
			if (h) *h = local.wHour;
			if (m) *m = local.wMinute;
			if (s) *s = local.wSecond;
			if (us) *us = local.wMilliseconds * 1000;
		}

		uint64_t duration_usec(uint64_t begin, uint64_t end)
		{
			// 1 tick = 100 ns -> 1000 
			return (end - begin) / 10;
		}
	}
	
	namespace fs
	{
		uint64_t get_modification_timestamp(const char *path)
		{
			uint64_t stamp = 0;
			HANDLE f = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (f != INVALID_HANDLE_VALUE)
			{
				FILETIME last_write;
				if (GetFileTime(f, nullptr, nullptr, &last_write))
				{
					stamp = (uint64_t)last_write.dwLowDateTime;
					stamp |= ((uint64_t)last_write.dwHighDateTime) << 32;
				}
				CloseHandle(f);
			}
			return stamp;
		}

		namespace detail
		{
			string_vector enumerate_fs_items(const char *path, const bool files)
			{
				string_vector path_elements = path::split(path);
				std::string& wildcard = path_elements.back();	// Assume that the final element is the wildcard.

				auto glob_it = std::find_if(path_elements.begin(), path_elements.end(), [](const std::string& s) { return s.compare("**") == 0; });
				const bool recursive = glob_it != path_elements.end();
				std::string root_path;
				for (auto it = path_elements.begin(); it != (recursive ? glob_it : (path_elements.end() - 1)); ++it)
					root_path = path::join(root_path, *it);

				string_vector found;
				auto find_impl = [](const std::string& root_path, const std::string& wildcard, std::function<void(const WIN32_FIND_DATAA& data, const std::string& parent)> visitor)
				{
					WIN32_FIND_DATAA data;
					HANDLE handle = FindFirstFileA(path::join(root_path, wildcard).c_str(), &data);
					if (handle != INVALID_HANDLE_VALUE)
					{
						do
						{
							if (0 != strcmp(data.cFileName, ".") && 0 != strcmp(data.cFileName, ".."))
							{
								visitor(data, root_path);
							}
						} while (FindNextFileA(handle, &data));

						FindClose(handle);
					}
				};

				find_impl(root_path, wildcard, [&found, files](const WIN32_FIND_DATAA& data, const std::string& parent)
				{
					if (files == !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					{
						found.push_back(path::join(parent, data.cFileName));
					}
				});
				if (recursive)
				{
					find_impl(root_path, "*", [&found, &wildcard, files](const WIN32_FIND_DATAA& data, const std::string& parent)
					{
						if (!!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						{
							string_vector subset = enumerate_fs_items(path::join(path::join(parent, data.cFileName), path::join("**", wildcard)).c_str(), files);
							found.insert(found.end(), subset.begin(), subset.end());
						}
					});
				}
				return found;
			}
		}

		string_vector enumerate_files(const char *path)
		{
			return detail::enumerate_fs_items(path, true);
		}

		string_vector enumerate_directories(const char *path)
		{
			return detail::enumerate_fs_items(path, false);
		}

		bool mkdir(const char *path, bool make_parent_directories)
		{
			if (make_parent_directories)
			{
				auto elements = path::split(path);
				std::string intermediate;
				for (auto& e : elements)
				{
					intermediate = path::join(intermediate, e);
					const bool full = (&e == &elements.back());
					if (!CreateDirectoryA(intermediate.c_str(), nullptr) && full)
					{
						return false;
					}
				}
				return true;
			}
			else
			{
				return CreateDirectoryA(path, nullptr);
			}
		}

		bool copy_file(const char *existing_path, const char *new_path, copy_flags flags)
		{
			if (CopyFileA(existing_path, new_path, (!!(flags & overwrite)) ? FALSE : TRUE))
			{
				cbl::log_verbose("Copied file %s to %s, copy flags 0x%X", existing_path, new_path, flags);
				if (!!(flags & maintain_timestamps))
				{
					HANDLE ef = CreateFileA(existing_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
					if (ef != INVALID_HANDLE_VALUE)
					{
						FILETIME stamps[3];
						if (GetFileTime(ef, stamps + 0, stamps + 1, stamps + 2))
						{
							HANDLE nf = CreateFileA(new_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
							if (nf != INVALID_HANDLE_VALUE)
							{
								if (!SetFileTime(nf, stamps + 0, stamps + 1, stamps + 2))
								{
									cbl::warning("Failed to set file access time on %s, "
										"the file might be erroneously treated as up-to-date\n",
										new_path);
								}
								CloseHandle(nf);
							}
						}
						CloseHandle(ef);
					}
				}
				return true;
			}
			cbl::warning("Failed to copy file %s to %s, copy flags 0x%X", existing_path, new_path, flags);
			return false;
		}

		bool move_file(const char *existing_path, const char *new_path, copy_flags flags)
		{
			if (MoveFileExA(existing_path, new_path, MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED | ((!!(flags & overwrite)) ? 0 : MOVEFILE_REPLACE_EXISTING)))
			{
				cbl::log_verbose("Moved file %s to %s, copy flags 0x%X", existing_path, new_path, flags);
				if (!!(flags & maintain_timestamps))
				{
					HANDLE ef = CreateFileA(existing_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
					if (ef != INVALID_HANDLE_VALUE)
					{
						FILETIME stamps[3];
						if (GetFileTime(ef, stamps + 0, stamps + 1, stamps + 2))
						{
							HANDLE nf = CreateFileA(new_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
							if (nf != INVALID_HANDLE_VALUE)
							{
								if (!SetFileTime(nf, stamps + 0, stamps + 1, stamps + 2))
								{
									cbl::warning("Failed to set file access time on %s, "
										"the file might be erroneously treated as up-to-date\n",
										new_path);
								}
								CloseHandle(nf);
							}
						}
						CloseHandle(ef);
					}
				}
				return true;
			}
			cbl::warning("Failed to move file %s to %s, copy flags 0x%X", existing_path, new_path, flags);
			return false;
		}

		bool delete_file(const char *path)
		{
			if (DeleteFileA(path))
			{
				cbl::log_verbose("Deleted file %s", path);
				return true;
			}
			else
			{
				cbl::warning("Failed to delete file %s", path);
				return true;
			}
		}

		void disinherit_stream(FILE *stream)
		{
			if (HANDLE h = (HANDLE)_get_osfhandle(_fileno(stream)))
			{
				SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
			}
		}
	}

	process::process()
		: handle{ INVALID_HANDLE_VALUE }
		, in{ INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE }
		, err{ INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE }
		, out{ INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE }
	{}

	deferred_process process::start_deferred(
		const char *not_owned_commandline,
		pipe_output_callback on_stderr,
		pipe_output_callback on_stdout,
		const std::vector<uint8_t> *stdin_buffer, void *environment)
	{
		std::string commandline = not_owned_commandline;
		// FIXME: Lifetime of callbacks, buffer & environment?
		auto kickoff = [=]() -> std::shared_ptr<process>
		{
			process *p = nullptr;
			auto safe_close_handles = [](HANDLE h[2])
			{
				if (h[pipe_write] != INVALID_HANDLE_VALUE)
					CloseHandle(h[pipe_write]);
				if (h[pipe_read] != INVALID_HANDLE_VALUE)
					CloseHandle(h[pipe_read]);
			};

			SECURITY_ATTRIBUTES sec_attr;
			sec_attr.nLength = sizeof(sec_attr);
			sec_attr.bInheritHandle = TRUE;
			sec_attr.lpSecurityDescriptor = nullptr;

			HANDLE in[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
			HANDLE out[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
			HANDLE err[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };

			auto safe_create_pipe = [&](HANDLE h[2], bool inherit_write) -> bool
			{
				if (!CreatePipe(&h[pipe_read], &h[pipe_write], &sec_attr, 0) ||
					!SetHandleInformation(h[inherit_write ? pipe_write : pipe_read], HANDLE_FLAG_INHERIT, 0))
				{
					safe_close_handles(in);
					safe_close_handles(err);
					safe_close_handles(out);
					return false;
				}
				return true;
			};

			if (stdin_buffer && !safe_create_pipe(in, true))
			{
				return nullptr;
			}
			if (on_stderr && !safe_create_pipe(err, false))
			{
				return nullptr;
			}
			if (on_stdout && !safe_create_pipe(out, false))
			{
				return nullptr;
			}

			PROCESS_INFORMATION proc_info = { 0 };
			STARTUPINFO start_info = { 0 };
			start_info.cb = sizeof(start_info);
			start_info.hStdError = err[pipe_write] != INVALID_HANDLE_VALUE ? err[pipe_write] : GetStdHandle(STD_ERROR_HANDLE);
			start_info.hStdOutput = out[pipe_write] != INVALID_HANDLE_VALUE ? out[pipe_write] : GetStdHandle(STD_OUTPUT_HANDLE);
			start_info.hStdInput = in[pipe_read] != INVALID_HANDLE_VALUE ? in[pipe_write] : GetStdHandle(STD_INPUT_HANDLE);
			start_info.dwFlags = STARTF_USESTDHANDLES;

			char cwd[260];
			GetCurrentDirectoryA(sizeof(cwd), cwd);

			if (!CreateProcessA(nullptr, const_cast<LPSTR>(commandline.c_str()), nullptr, nullptr, TRUE, 0, environment, cwd, &start_info, &proc_info))
			{
				auto reason = win64::get_last_error_str();

				cbl::error("Failed to launch: %s", commandline.c_str());
				cbl::error("Reason: %s", reason.c_str());

				safe_close_handles(in);
				safe_close_handles(out);
				safe_close_handles(err);
				return nullptr;
			}

			cbl::log_verbose("Launched process #%d, handle #%d: %s", proc_info.dwProcessId, (uintptr_t)proc_info.hProcess, commandline.c_str());

			if (stdin_buffer)
			{
				DWORD written;
				WriteFile(in[pipe_write], stdin_buffer->data(), stdin_buffer->size(), &written, nullptr);
			}

			CloseHandle(proc_info.hThread);	// We don't care about the child process' main thread.

			p = new process();
			p->on_err = on_stderr;
			p->on_out = on_stdout;
			p->handle = proc_info.hProcess;
			memcpy(p->in, in, sizeof(p->in));
			memcpy(p->out, out, sizeof(p->out));
			memcpy(p->err, err, sizeof(p->err));
			return std::shared_ptr<process>(p);
		};
		return kickoff;
	}

	int process::wait()
	{
		MTR_SCOPE_I(__FILE__, "Wait for process", "handle", (uintptr_t)handle);

		int exit_code = -1;
		auto read_pipe_to_callback = [](HANDLE pipe[2], std::vector<uint8_t> &buffer, pipe_output_callback& cb)
		{
			if (pipe[pipe_read] != INVALID_HANDLE_VALUE)
			{
				BOOL result = TRUE;
				DWORD available = 0;
				if (PeekNamedPipe(pipe[pipe_read], nullptr, 0, nullptr, &available, nullptr) && available > 0)
				{
					DWORD read;
					buffer.resize(available);
					if (ReadFile(pipe[pipe_read], buffer.data(), available, &read, nullptr))
					{
						cb(buffer.data(), read);
					}
				}
			}
		};

		std::vector<uint8_t> buffer;

		size_t handle_count = 1;
		HANDLE handles[1 + 2];
		handles[0] = handle;
		auto collect_pipe = [&handles, &handle_count](HANDLE pipe[2])
		{
			if (INVALID_HANDLE_VALUE != pipe[pipe_read])
				handles[handle_count++] = pipe[pipe_read];
		};
		collect_pipe(out);
		collect_pipe(err);

		DWORD result;
		do
		{
			// Clogged pipes may stop the process from completing. Keep polling until handle at index 0 (i.e. the process) completes.
			result = WaitForMultipleObjects(handle_count, handles, FALSE, INFINITE);
			read_pipe_to_callback(err, buffer, on_err);
			read_pipe_to_callback(out, buffer, on_out);
		} while (result != WAIT_OBJECT_0 && result != WAIT_FAILED);
		GetExitCodeProcess(handle, (LPDWORD)(&exit_code));
		
		// Make sure to drain the pipes.
		if (handle_count > 1)
		{
			WaitForMultipleObjects(handle_count - 1, handles + 1, TRUE, INFINITE);
			read_pipe_to_callback(err, buffer, on_err);
			read_pipe_to_callback(out, buffer, on_out);
		}

		CloseHandle(handle);
		auto safe_close_handles = [](HANDLE h[2])
		{
			if (h[pipe_write] != INVALID_HANDLE_VALUE)
				CloseHandle(h[pipe_write]);
			if (h[pipe_read] != INVALID_HANDLE_VALUE)
				CloseHandle(h[pipe_read]);
		};
		safe_close_handles(in);
		safe_close_handles(out);
		safe_close_handles(err);
		return exit_code;
	}

	void process::detach()
	{
		cbl::log_verbose("Detaching process handle #%d", (uintptr_t)handle);
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
		auto safe_close_handles = [](HANDLE h[2])
		{
			if (h[pipe_write] != INVALID_HANDLE_VALUE)
			{
				CloseHandle(h[pipe_write]);
				h[pipe_write] = INVALID_HANDLE_VALUE;
			}
			if (h[pipe_read] != INVALID_HANDLE_VALUE)
			{
				CloseHandle(h[pipe_read]);
				h[pipe_write] = INVALID_HANDLE_VALUE;
			}
		};
		safe_close_handles(in);
		safe_close_handles(out);
		safe_close_handles(err);
	}

	std::vector<int> process::wait_for_multiple(const std::vector<std::shared_ptr<process>>& processes)
	{
		std::vector<int> exit_codes;
		// FIXME: This can be made to run much more concurrently than this with WaitForMultipleObjects.
		for (auto p : processes)
		{
			exit_codes.push_back(p->wait());
		}
		return exit_codes;
	}

	uint32_t process::get_current_pid()
	{
		return (uint32_t)GetCurrentProcessId();
	}

	std::string process::get_current_executable_path()
	{
		std::string s;
		do
		{
			s.resize(s.size() + 260);
			DWORD length = GetModuleFileNameA(nullptr, const_cast<LPSTR>(s.data()), s.size());
		} while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);
		// On some versions of Windows, the string may not be null-terminated.
		if (s.back() != 0)
		{
			s.push_back(0);
		}
		// Trim to the actually used characters.
		s.resize(strlen(s.c_str()));
		return s;
	}

	void process::wait_for_pid(uint32_t pid)
	{
		if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid))
		{
			WaitForSingleObject(h, INFINITE);
		}
	}

	namespace win64
	{
		namespace registry
		{
			bool read_key(hkey root_key, const char *sub_key, const char *value_name, void *in_buffer, size_t& in_out_size, const type& expected_type)
			{
				auto hkey_to_enum = [](hkey k)
				{
					switch (k)
					{
					case hkey::classes_root: return HKEY_CLASSES_ROOT;
					case hkey::current_config: return HKEY_CURRENT_CONFIG;
					case hkey::current_user: return HKEY_CURRENT_USER;
					case hkey::local_machine: return HKEY_LOCAL_MACHINE;
					case hkey::users: return HKEY_USERS;
					default: assert(!"Invalid key"); return (HKEY)0;
					}
				};

				auto type_conforms = [](decltype(REG_DWORD) windows_type, type cbl_type)
				{
					switch (cbl_type)
					{
					case type::binary_blob: return true;
					case type::dword: return windows_type == REG_DWORD || windows_type == REG_DWORD_BIG_ENDIAN || windows_type == REG_DWORD_LITTLE_ENDIAN;
					case type::string: return windows_type == REG_SZ || windows_type == REG_LINK || windows_type == REG_EXPAND_SZ;
					case type::multiple_strings: return windows_type == REG_MULTI_SZ;
					case type::qword: return windows_type == REG_QWORD || windows_type == REG_QWORD_LITTLE_ENDIAN;
					default: assert(!"Invalid type"); return false;
					}
				};

				auto enum_to_type = [](decltype(REG_DWORD) windows_type)
				{
					switch (windows_type)
					{
					case REG_DWORD:
					case REG_DWORD_BIG_ENDIAN:
					//case REG_DWORD_LITTLE_ENDIAN:
						return type::dword;
					case REG_SZ:
					case REG_LINK:
					case REG_EXPAND_SZ:
						return type::string;
					case REG_MULTI_SZ:
						return type::multiple_strings;
					case REG_QWORD:
					//case REG_QWORD_LITTLE_ENDIAN:
						return type::qword;
					default:
						return type::binary_blob;
					}
				};

				HKEY handle;
				LSTATUS result;
				result = RegOpenKeyExA(hkey_to_enum(root_key), sub_key, 0, KEY_READ, &handle);
				if (result == ERROR_SUCCESS)
				{
					DWORD buffer_length = 0;
					DWORD type = 0;
					result = RegQueryValueExA(handle, value_name, nullptr, &type, nullptr, &buffer_length);
					if (result == ERROR_SUCCESS)
					{
						if (buffer_length <= in_out_size && type_conforms(type, expected_type))
						{
							in_out_size = buffer_length;
							result = RegQueryValueExA(handle, value_name, nullptr, &type, (BYTE*)in_buffer, &buffer_length);
							if (result == ERROR_SUCCESS)
							{
								RegCloseKey(handle);
								return true;
							}
						}
					}
					RegCloseKey(handle);
				}
				
				return false;
			}

			bool try_read_software_key(const char *sub_key, const char *value_name, void *in_buffer, size_t& in_out_size, const type& expected_type)
			{
				static constexpr const char software[] = "SOFTWARE";
				static constexpr const char wow64[] = "WOW6432Node";

				if (read_key(hkey::current_user, path::join(software, sub_key).c_str(), value_name, in_buffer, in_out_size, expected_type))
					return true;
				else if(read_key(hkey::local_machine, path::join(software, sub_key).c_str(), value_name, in_buffer, in_out_size, expected_type))
					return true;
				else if (read_key(hkey::current_user, path::join(path::join(software, wow64), sub_key).c_str(), value_name, in_buffer, in_out_size, expected_type))
					return true;
				else if (read_key(hkey::local_machine, path::join(path::join(software, wow64), sub_key).c_str(), value_name, in_buffer, in_out_size, expected_type))
					return true;
				return false;
			}

			bool try_read_software_path_key(const char *sub_key, const char *value_name, std::string &in_out_path)
			{
				in_out_path.resize(MAX_PATH);
				size_t size = in_out_path.size();
				if (try_read_software_key(sub_key, value_name, (void *)in_out_path.data(), size, type::string))
				{
					in_out_path.resize(size - 1);
					return true;
				}
				return false;
			}
			
		}

		namespace debug
		{
			std::string get_pdb_path_for_module(uintptr_t base_pointer)
			{
				// Source: https://deplinenoise.wordpress.com/2013/06/14/getting-your-pdb-name-from-a-running-executable-windows/

				struct pdb_info
				{
					DWORD signature;
					BYTE guid[16];
					DWORD age;
					char filename[1];
				};

				IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base_pointer;
				IMAGE_FILE_HEADER* file_header = (IMAGE_FILE_HEADER*)(base_pointer + dos_header->e_lfanew + 4);
				if (file_header->SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER))
				{
					IMAGE_OPTIONAL_HEADER* opt_header = (IMAGE_OPTIONAL_HEADER*)(((char*)file_header) + sizeof(IMAGE_FILE_HEADER));
					IMAGE_DATA_DIRECTORY* dir = &opt_header->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
					IMAGE_DEBUG_DIRECTORY* dbg_dir = (IMAGE_DEBUG_DIRECTORY*)(base_pointer + dir->VirtualAddress);
					if (IMAGE_DEBUG_TYPE_CODEVIEW == dbg_dir->Type)
					{
						auto* info = (pdb_info*)(base_pointer + dbg_dir->AddressOfRawData);
						if (0 == memcmp(&info->signature, "RSDS", 4))
						{
							return info->filename;
						}
					}
				}
				return std::string();
			}

			void filter_own_pdb(string_vector& paths)
			{
				auto pdb = get_pdb_path_for_module((uintptr_t)GetModuleHandle(NULL));
				if (!pdb.empty())
				{
					std::string needle = path::get_filename(pdb.c_str());
					for (auto begin = paths.begin();;)
					{
						auto it = std::find_if(paths.begin(), paths.end(),
							[&needle](const auto &p) { return path::get_filename(p.c_str()) == needle; });
						if (it == paths.end())
							break;
						else
						{
							auto offset = it - paths.begin();
							paths.erase(it);
							begin = paths.begin() + offset;
						}
					}
				}
			}
		}

		bool wide_str_to_utf8_str(std::string& utf8, wchar_t *wide)
		{
			utf8.resize(wcslen(wide) + 1);
			for (;;)
			{
				if (int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, (LPSTR)utf8.data(), utf8.size(), nullptr, nullptr))
				{
					utf8.resize(written - 1);
					utf8.shrink_to_fit();
					return true;
				}
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
					return false;
				utf8.resize(utf8.size() * 2);
			}
			return false;
		}
	}
}

static HANDLE g_job_object;

void init_process_group()
{
	constexpr const char job_name[] = "cppbuild";	// FIXME: This will be a problem with multiple independent builds.

	SECURITY_ATTRIBUTES sec_attr;
	sec_attr.nLength = sizeof(sec_attr);
	sec_attr.bInheritHandle = TRUE;
	sec_attr.lpSecurityDescriptor = nullptr;

	g_job_object = CreateJobObjectA(&sec_attr, job_name);
	if (!g_job_object)
	{
		auto reason = cbl::win64::get_last_error_str();
		cbl::log_verbose("Failed to branch off a process group, reason: %s", reason.c_str());
	}
	else
	{
		if (!AssignProcessToJobObject(g_job_object, GetCurrentProcess()))
		{
			auto reason = cbl::win64::get_last_error_str();
			cbl::log_verbose("Failed to assign self to process group, reason: %s", reason.c_str());
		}
	}

	atexit([]() { CloseHandle(g_job_object); });
}

void terminate_process_group(int exit_code)
{
	if (!TerminateJobObject(g_job_object, exit_code))
	{
		auto reason = cbl::win64::get_last_error_str();
		cbl::log_verbose("Failed to terminate process group, reason: %s", reason.c_str());
		exit(exit_code);
	}
}

#endif	// defined(_WIN64)
