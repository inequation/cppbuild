#pragma once

#include <cctype>

// Includes here are for the benefit of syntax highlighting systems, #pragma once prevents recursion.
#include "../cppbuild.h"

namespace cbl
{
	constexpr platform get_host_platform()
	{
#if defined(_WIN32)
		return platform::win64;
#else
	#error Unsupported platform
#endif
	}

	constexpr const char* get_default_toolchain_for_host()
	{
		constexpr const char *toolchain_names[] =
		{
			"msvc",	// win64
			"gcc"	// linux
		};
		return toolchain_names[(uint8_t)get_host_platform()];
	}

	namespace path
	{
		constexpr const char get_path_separator()
		{
			return get_host_platform() == platform::win64 ? '\\' : '/';
		}

		bool is_path_separator(char c)
		{
			return c == '/'
#ifdef _WIN32
				|| c == '\\'
#endif
				;
		}

		string_vector split(const char *path)
		{
			string_vector elements;
			size_t len = strlen(path);
			const char *begin = path;
			for (size_t i = 0; i < len; ++i)
			{
				if (path + i < begin)
				{
					continue;
				}
				if (is_path_separator(path[i]))
				{
					if (i == 0)
					{
						continue;
					}

					std::string new_element(begin, path + i - begin);
					if (!elements.empty() && elements.back().length() == 0)
					{
						elements.back() = std::move(new_element);
					}
					else
					{
						elements.push_back(new_element);
					}

					begin = path + i + 1;
				}
			}
			if (*begin)
			{
				elements.push_back(begin);
			}
			return elements;
		}

		std::string get_extension(const char *path)
		{
			if (const char* dot = strrchr(path, '.'))
			{
				return std::string(dot + 1);
			}
			else
			{
				return std::string();
			}
		}

		std::string get_path_without_extension(const char *path)
		{
			if (const char* dot = strrchr(path, '.'))
			{
				return std::string(path, dot - path);
			}
			else
			{
				return std::string(path);
			}
		}

		std::string join(const std::string& a, const std::string& b)
		{
			if (!a.empty() && *(a.end() - 1) != get_path_separator())
			{
				return a + get_path_separator() + b;
			}
			else
			{
				return a + b;
			}
		}
	};

	uint64_t now()
	{
		uint64_t stamp = 0;
#ifdef _WIN32
		SYSTEMTIME st;
		FILETIME ft;
		GetSystemTime(&st);
		if (SystemTimeToFileTime(&st, &ft))
		{
			stamp = (uint64_t)ft.dwLowDateTime;
			stamp |= ((uint64_t)ft.dwHighDateTime) << 32;
		}
#else
	#error Unsupported platform
#endif
		return stamp;
	}
	
	uint64_t get_modification_timestamp(const char *path)
	{
		uint64_t stamp = 0;
#ifdef _WIN32
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
#else
	#error Unsupported platform
#endif
		return stamp;
	}

	string_vector vwrap(const std::string& s)
	{
		string_vector v;
		v.emplace_back(s);
		return v;
	}

	std::function<string_vector()> fvwrap(const std::string& s)
	{
		return [s]() { return vwrap(s); };
	}

	void trim(std::string& s)
	{
		if (!s.empty())
		{
			int l = 0, r = s.size() - 1;

			while (l < s.size() && std::isspace(s[l++]));
			while (r >= 0 && std::isspace(s[r--]));

			if (l > r)
			{
				s.clear();
			}
			else
			{
				l--;
				r++;
				int wi = 0;
				while (l <= r)
				{
					s[wi++] = s[l++];
				}
				s.erase(wi);
			}
		}
	}

	std::string join(const string_vector& v, const char *glue)
	{
		std::string result;
		for (const auto& s : v)
		{
			if (!result.empty())
				result += glue;
			result += s;
		}
		return result;
	}

	string_vector enumerate_files(const char *path)
	{
		string_vector path_elements = path::split(path);
		std::string& wildcard = path_elements.back();	// Assume that the final element is the wildcard.

		auto glob_it = std::find_if(path_elements.begin(), path_elements.end(), [](const std::string& s) { return s.compare("**") == 0; });
		const bool recursive = glob_it != path_elements.end();
		std::string root_path;
		for (auto it = path_elements.begin(); it != (recursive ? glob_it : (path_elements.end() - 1)); ++it)
			root_path = path::join(root_path, *it);

		string_vector found;
#ifdef _WIN32
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

		find_impl(root_path, wildcard, [&found](const WIN32_FIND_DATAA& data, const std::string& parent)
		{
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				found.push_back(path::join(parent, data.cFileName));
			}
		});
		if (recursive)
		{
			find_impl(root_path, "*", [&found, &wildcard](const WIN32_FIND_DATAA& data, const std::string& parent)
			{
				if (!!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					string_vector subset = enumerate_files(path::join(path::join(parent, data.cFileName), path::join("**", wildcard)).c_str());
					found.insert(found.end(), subset.begin(), subset.end());
				}
			});
		}
#else
	#error Unsupported platform
#endif
		return found;
	}

	bool mkdir(const char *path, bool make_parent_directories)
	{
#if defined(_WIN32)
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
#else
	#error Unsupported platform
#endif
	}

	enum copy_flags 
	{
		overwrite			= 0x1,
		maintain_timestamps = 0x2
	};
	bool copy_file(const char *existing_path, const char *new_path, copy_flags flags)
	{
#if defined(_WIN32)
		if (CopyFileA(existing_path, new_path, (!!(flags & overwrite)) ? FALSE : TRUE))
		{
			if (!!(flags & maintain_timestamps))
			{
				HANDLE ef = CreateFileA(existing_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (ef != INVALID_HANDLE_VALUE)
				{
					FILETIME last_write;
					if (GetFileTime(ef, nullptr, nullptr, &last_write))
					{
						HANDLE nf = CreateFileA(existing_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
						if (nf != INVALID_HANDLE_VALUE)
						{
							SetFileTime(nf, nullptr, nullptr, &last_write);
						}
						CloseHandle(nf);
					}
					CloseHandle(ef);
				}
			}
			return true;
		}
		return false;
#else
	#error Unsupported platform
#endif
	}

	namespace base_configurations
	{
		static const configuration debug()
		{
			static configuration c;
			c.emit_debug_information = true;
			c.optimize = configuration::O1;
			c.definitions.push_back(std::make_pair("_DEBUG", ""));
			return c;
		};

		static const configuration release()
		{
			static configuration c;
			c.emit_debug_information = true;
			c.optimize = configuration::O2;
			c.definitions.push_back(std::make_pair("NDEBUG", "1"));
			return c;
		};

		static const configuration shipping()
		{
			static configuration c;
			c.emit_debug_information = true;
			c.optimize = configuration::O3;
			c.definitions.push_back(std::make_pair("NDEBUG", "2"));
			return c;
		};
	};

	struct process
	{
		pipe_output_callback on_out, on_err;
#if _WIN32
		HANDLE handle = INVALID_HANDLE_VALUE;
		HANDLE in[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
		HANDLE out[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
		HANDLE err[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
#else
	#error Unsupported platform
#endif
	private:
		process() = default;
		enum { pipe_read, pipe_write };
	public:
		static std::shared_ptr<process> start_async(const char *commandline,
			pipe_output_callback on_stderr = nullptr,
			pipe_output_callback on_stdout = nullptr,
			const std::vector<uint8_t> *stdin_buffer = nullptr, void *environment = nullptr)
		{
			process *p = nullptr;
#if _WIN32
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
			start_info.hStdError = err[pipe_write] != INVALID_HANDLE_VALUE ? err[pipe_write] : 0;
			start_info.hStdOutput = out[pipe_write] != INVALID_HANDLE_VALUE ? out[pipe_write] : 0;
			start_info.hStdInput = in[pipe_read] != INVALID_HANDLE_VALUE ? in[pipe_write] : 0;
			start_info.dwFlags |= STARTF_USESTDHANDLES;

			char cwd[260];
			GetCurrentDirectoryA(sizeof(cwd), cwd);

			if (!CreateProcessA(nullptr, const_cast<LPSTR>(commandline), nullptr, nullptr, TRUE, 0, environment, cwd, &start_info, &proc_info))
			{
				DWORD error = GetLastError();
				char buffer[256];
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					buffer, (sizeof(buffer) / sizeof(buffer[0])), nullptr);
				safe_close_handles(in);
				safe_close_handles(out);
				safe_close_handles(err);
				return nullptr;
			}

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
#else
	#error Unsupported platform
#endif
			return std::shared_ptr<process>(p);
		}

		int wait()
		{
			int exit_code = -1;
#if _WIN32
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
							cb(buffer.data(), buffer.size());
						}
					}
				}
			};

			std::vector<uint8_t> buffer;

			size_t handle_count = 1;
			HANDLE handles[1 + 2];
			handles[0] = handle;
			auto collect_pipe = [&](HANDLE pipe[2]) { if (out[pipe_read]) { handles[handle_count++] = out[pipe_read]; } };
			collect_pipe(out);
			collect_pipe(err);

			DWORD result;
			do
			{
				// Clogged pipes may stop the process from completing. Keep polling until handle at index 0 (i.e. the process) completes.
				result = WaitForMultipleObjects(handle_count, handles, FALSE, INFINITY);
				read_pipe_to_callback(err, buffer, on_err);
				read_pipe_to_callback(out, buffer, on_out);
			} while (result != WAIT_OBJECT_0 && result != WAIT_FAILED);
			GetExitCodeProcess(handle, (LPDWORD)(&exit_code));
			
			read_pipe_to_callback(err, buffer, on_err);
			read_pipe_to_callback(out, buffer, on_out);

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
#else
	#error Unsupported platform
#endif
			return exit_code;
		}

		void detach()
		{
#if defined(_WIN32)
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
#else
	#error Unsupported platform
#endif
		}

		static std::vector<int> wait_for_multiple(const std::vector<std::shared_ptr<process>>& processes)
		{
			std::vector<int> exit_codes;
			// FIXME: This can be made to run much more concurrently than this with WaitForMultipleObjects.
			for (auto p : processes)
			{
				exit_codes.push_back(p->wait());
			}
			return exit_codes;
		}

		static uint32_t get_current_pid()
		{
#if defined(_WIN32)
			return (uint32_t)GetCurrentProcessId();
#else
	#error Unsupported platform
#endif
		}

		static std::string get_current_executable_path()
		{
#if defined(_WIN32)
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
			return s;
#else
	#error Unsupported platform
#endif
		}

		static void wait_by_pid(uint32_t pid)
		{
#if defined(_WIN32)
			if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid))
			{
				assert(!"FIXME!!! This wait returns immediately. :(");
				WaitForSingleObject(h, INFINITY);
			}
#else
	#error Unsupported platform
#endif
		}
	};
}
