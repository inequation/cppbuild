#if !defined(__linux__) && defined(__INTELLISENSE__)
	#define __linux__	// Get correct syntax highlighting in MSVS.
#endif

#if defined(__linux__)

#include "../cppbuild.h"
#include "../cbl.h"

#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <sched.h>
#include <spawn.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <wordexp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace cbl
{
	namespace path
	{
		bool is_path_separator(char c) { return c == '/'; }

		std::string get_absolute(const char *path)
		{
			std::string abs;
			abs.resize(PATH_MAX);
			if (!realpath(path, (char *)abs.data()))
			{
				int error = errno;
				cbl::log_verbose("Failed to get absolute path for %s, reason: %s", path, strerror(error));
				return "";
			}
			abs.resize(strlen(abs.c_str()));
			return abs;
		}

		std::string get_working_path()
		{
			std::string abs;
			abs.resize(PATH_MAX);
			if (!getcwd((char *)abs.data(), abs.size()))
			{
				int error = errno;
				cbl::log_verbose("Failed to get working path, reason: %s", strerror(error));
				return "";
			}
			abs.resize(strlen(abs.c_str()));
			return abs;
		}
	};

	namespace time
	{
		// NOTE: This must be meaningfully comparable with fs::get_modification_timestamp()!
		uint64_t now()
		{
			uint64_t stamp = 0;
			timeval tv;
			if (!gettimeofday(&tv, nullptr))
			{
				stamp = tv.tv_sec * 1000 * 1000;
				stamp += tv.tv_usec;
			}
			return stamp;
		}

		void of_day(const uint64_t stamp, int *y, int *M, int *d, int *h, int *m, int *s, int *us)
		{
			time_t time = stamp / 1000 / 1000;
			struct tm local;
			if (struct tm *static_alloc = localtime(&time))
				local = *static_alloc;

			if (y) *y = local.tm_year + 1900;
			if (M) *M = local.tm_mon + 1;
			if (d) *d = local.tm_mday;
			if (h) *h = local.tm_hour;
			if (m) *m = local.tm_min;
			if (s) *s = local.tm_sec;
			if (us) *us = stamp % 1000;
		}

		uint64_t duration_usec(uint64_t begin, uint64_t end)
		{
			// 1 tick = 1 us
			return (end - begin);
		}
	}
	
	namespace fs
	{
		uint64_t get_modification_timestamp(const char *path)
		{
			uint64_t stamp = 0;
			struct stat s;
			if (!stat(path, &s))
			{
				stamp = s.st_mtim.tv_sec * 1000 * 1000;
				stamp += s.st_mtim.tv_nsec / 1000;
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
				auto find_impl = [](const std::string& root_path, const std::string& wildcard, const bool files, std::function<void(const std::string& parent)> visitor)
				{
					int glob_flags = GLOB_MARK | GLOB_NOSORT | (files ? 0 : GLOB_ONLYDIR);
					glob_t glob = { 0, nullptr, 0 };
					int result = ::glob(path::join(root_path, wildcard).c_str(), glob_flags, nullptr, &glob);
					if (!result)
					{
						for (size_t i = 0; i < glob.gl_pathc; ++i)
						{
							char *c_str = glob.gl_pathv[i];
							std::string str(c_str);
							visitor(str);
						}
					}
					::globfree(&glob);
				};

				find_impl(root_path, wildcard, files, [&found, files](const std::string& p)
				{
					if (path::is_path_separator(p.back()) != files)
					{
						found.push_back(p);
					}
				});
				if (recursive)
				{
					find_impl(root_path, "*", files, [&found, &wildcard, files](const std::string& p)
					{
						if (path::is_path_separator(p.back()))
						{
							string_vector subset = enumerate_fs_items(path::join(p, path::join("**", wildcard)).c_str(), files);
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
					int result = ::mkdir(intermediate.c_str(), 0755);
					if (result < 0)
					{
						if (errno == EEXIST)
						{
							if (full)
								return true;
						}
						else
							return false;
					}
				}
				return true;
			}
			else
			{
				return !mkdir(path, 0755);
			}
		}

		bool copy_file(const char *existing_path, const char *new_path, copy_flags flags)
		{
			struct scoped_fd
			{
				~scoped_fd()
				{
					if (fd != -1)
						close(fd);
				}
				int fd;
			};

			scoped_fd from{ open(existing_path, O_RDONLY) };
			if (from.fd < 0)
				return -1;
			struct stat s;
			if (fstat(from.fd, &s) < 0)
				return false;

			void *mem = mmap(nullptr, s.st_size, PROT_READ, MAP_SHARED, from.fd, 0);
			if (mem == MAP_FAILED)
				return false;

			cbl::scoped_guard cleanup([mem, s]() { munmap(mem, s.st_size); });

			int fd_to = open(new_path, O_CREAT | O_WRONLY | (!!(flags & overwrite) ? O_TRUNC : 0), 0666);
			if (fd_to < 0)
				return false;

			ssize_t nwritten = write(fd_to, mem, s.st_size);
			if (nwritten < s.st_size)
				return false;

			if (close(fd_to) < 0) {
				fd_to = -1;
				return false;
			}

			if (flags & maintain_timestamps)
			{
				timeval tv[2];
				tv[0].tv_sec = s.st_atim.tv_sec;
				tv[0].tv_usec = s.st_atim.tv_nsec / 1000;
				tv[1].tv_sec = s.st_mtim.tv_sec;
				tv[1].tv_usec = s.st_mtim.tv_nsec / 1000;
				if (utimes(new_path, tv) < 0)
					return false;
			}

			return true;
		}

		bool move_file(const char *existing_path, const char *new_path, copy_flags flags)
		{
			struct stat s;
			if (stat(existing_path, &s) < 0)
				return false;
			if (!rename(existing_path, new_path))
			{
				if (!!(flags & maintain_timestamps))
				{
					timeval tv[2];
					tv[0].tv_sec = s.st_atim.tv_sec;
					tv[0].tv_usec = s.st_atim.tv_nsec / 1000;
					tv[1].tv_sec = s.st_mtim.tv_sec;
					tv[1].tv_usec = s.st_mtim.tv_nsec / 1000;
					if (utimes(new_path, tv) < 0)
						return false;
					return true;
				}
			}
			return false;
		}

		bool delete_file(const char *path)
		{
			if (unlink(path) == 0)
			{
				cbl::log_verbose("Deleted file %s", path);
				return true;
			}
			else
			{
				int error = errno;
				cbl::warning("Failed to delete file %s", path);
				cbl::log_verbose("Reason: %s", strerror(error));
				return true;
			}
			return false;
		}

		void disinherit_stream(FILE *stream)
		{
			if (int fd = fileno(stream))
			{
				int flags = fcntl(fd, F_GETFD);
				if (flags >= 0)
					fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
			}
		}
	}

	process::process()
	{}

	deferred_process process::start_deferred(
		const char *not_owned_commandline,
		pipe_output_callback on_stderr,
		pipe_output_callback on_stdout,
		const std::vector<uint8_t> *stdin_buffer, void *environment)
	{
		std::string commandline = not_owned_commandline;
		// FIXME: Lifetime of callbacks, buffer & environment?
		auto kickoff = [=]()->std::shared_ptr<process>
		{
			process *p = nullptr;
			auto safe_close_pipes = [](int p[2])
			{
				if (p[pipe_write] != -1)
					close(p[pipe_write]);
				if (p[pipe_read] != -1)
					close(p[pipe_read]);
			};

			int in[2] = { -1, -1 };
			int out[2] = { -1, -1 };
			int err[2] = { -1, -1 };

			auto safe_create_pipe = [&](int p[2], bool inherit_write) -> bool
			{
				if (pipe2(p, O_NONBLOCK) < 0)
				{
					safe_close_pipes(in);
					safe_close_pipes(err);
					safe_close_pipes(out);
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

			posix_spawn_file_actions_t actions;
			posix_spawn_file_actions_init(&actions);
			if (in[pipe_read] != -1)
				posix_spawn_file_actions_adddup2(&actions, in[pipe_read], STDIN_FILENO);
			if (out[pipe_write] != -1)
				posix_spawn_file_actions_adddup2(&actions, out[pipe_write], STDOUT_FILENO);
			if (err[pipe_write] != -1)
				posix_spawn_file_actions_adddup2(&actions, out[pipe_write], STDERR_FILENO);

			posix_spawnattr_t attr;
			posix_spawnattr_init(&attr);

			wordexp_t args = { 0, nullptr, 0 };
			if (wordexp(commandline.c_str(), &args, WRDE_UNDEF) < 0 || args.we_wordc < 1)
			{
				wordfree(&args);
				return nullptr;
			}

			// Exported by libc.
			std::vector<char *> ptrs;
			char **env = environ;
			if (environment)
			{
				char *s;
				for (s = (char *)environment; *s; s += strlen(s) + 1);
				{
					ptrs.push_back(s);
				}
				env = ptrs.data();
			}

			cbl::scoped_guard cleanup([&]()
			{
				posix_spawnattr_destroy(&attr);
				posix_spawn_file_actions_destroy(&actions);
				wordfree(&args);
			});

			pid_t child_pid = 0;
			if (posix_spawn(&child_pid, args.we_wordv[0], &actions, &attr, args.we_wordv, env) < 0)
			{
				int error = errno;

				cbl::error("Failed to launch: %s", commandline.c_str());
				cbl::error("Reason: %s", strerror(error));

				safe_close_pipes(in);
				safe_close_pipes(out);
				safe_close_pipes(err);

				return nullptr;
			}

			cbl::log_verbose("Launched process #%d: %s", child_pid, commandline.c_str());

			if (stdin_buffer)
				write(in[pipe_write], stdin_buffer->data(), stdin_buffer->size());

			p = new process();
			p->on_err = on_stderr;
			p->on_out = on_stdout;
			p->handle = (void *)(intptr_t)child_pid;
			auto copy_pipe = [](void *dst[2], int src[2])
			{
				dst[0] = (void *)(intptr_t)src[0];
				dst[1] = (void *)(intptr_t)src[1];
			};
			copy_pipe(p->in, in);
			copy_pipe(p->out, out);
			copy_pipe(p->err, err);
			return std::shared_ptr<process>(p);
		};
		return kickoff;
	}

	int process::wait()
	{
		MTR_SCOPE_I(__FILE__, "Wait for process", "handle", (uintptr_t)handle);

		auto read_pipe_to_callback = [](void *pipe[2], std::vector<uint8_t> &buffer, pipe_output_callback& cb)
		{
			if ((int)(intptr_t)pipe[pipe_read] != -1)
			{
				ssize_t read = ::read((int)(intptr_t)pipe[pipe_read], buffer.data(), buffer.size());
#define POLL_PIPES	0
#if POLL_PIPES
				if (read == -1)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						return false;
					else
						return true;
				}
				else if (read == 0)
				{
					return true;
				}
				cb(buffer.data(), read);
				return false;
			}
			return true;
#else
				if (read > 0)
					cb(buffer.data(), read);
				return read;
			}
			return ssize_t(-1);
#endif
		};

		std::vector<uint8_t> buffer;
		buffer.resize(32);

		int result = 0;
		int wstatus = 0;
		do
		{
			// Clogged pipes may stop the process from completing. Keep polling until handle at index 0 (i.e. the process) completes.
			while (read_pipe_to_callback(err, buffer, on_err) > 0);
			while (read_pipe_to_callback(out, buffer, on_out) > 0);
			constexpr int flags = POLL_PIPES ? WNOHANG : 0;
			result = waitpid((pid_t)(intptr_t)handle, &wstatus, flags);
			if (!!(flags & WNOHANG) && !result)
				sched_yield();
		} while (!result);

		// Make sure to drain the pipes.
		while (read_pipe_to_callback(err, buffer, on_err) > 0);
		while (read_pipe_to_callback(out, buffer, on_out) > 0);
		
		auto safe_close_pipes = [](void *p[2])
		{
			if ((int)(intptr_t)p[pipe_write] != -1)
				close((int)(intptr_t)p[pipe_write]);
			if ((int)(intptr_t)p[pipe_read] != -1)
				close((int)(intptr_t)p[pipe_read]);
		};
		safe_close_pipes(in);
		safe_close_pipes(out);
		safe_close_pipes(err);
		return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
	}

	void process::detach()
	{
		cbl::log_verbose("Detaching process handle #%d", (uintptr_t)handle);
		handle = (void *)-1;
		auto safe_close_pipes = [](void *p[2])
		{
			if ((int)(intptr_t)p[pipe_write] != -1)
				close((int)(intptr_t)p[pipe_write]);
			if ((int)(intptr_t)p[pipe_read] != -1)
				close((int)(intptr_t)p[pipe_read]);
		};
		safe_close_pipes(in);
		safe_close_pipes(out);
		safe_close_pipes(err);
	}

	std::vector<int> process::wait_for_multiple(const std::vector<std::shared_ptr<process>>& processes)
	{
		std::vector<int> exit_codes;
		// FIXME: This can be made to run much more concurrently than this.
		for (auto p : processes)
		{
			exit_codes.push_back(p->wait());
		}
		return exit_codes;
	}

	uint32_t process::get_current_pid()
	{
		return getpid();
	}

	std::string process::get_current_executable_path()
	{
		std::string s;
		s.resize(256);
		for (;;)
		{
			if (readlink("/proc/self/exe", (char *)s.data(), s.size()) < 0)
			{
				if (errno == ENAMETOOLONG)
					s.resize(2 * s.size());
				else
					break;
			}
			else
				break;
		}
		s.resize(strlen(s.c_str()));
		return s;
	}

	void process::wait_for_pid(uint32_t pid)
	{
		int wstatus = 0;
		if (waitpid((pid_t)pid, &wstatus, 0) < 0)
		{
			int error = errno;
			if (error == ECHILD)
			{
				// FIXME: Polling sucks, there must be an event to listen to.
				cbl::log_verbose("Pid %d isn't a child, falling back to procfs polling");
				std::string proc_path("/proc/" + std::to_string(pid));
				while (access(proc_path.c_str(), F_OK) == 0)
				{
#if 1				// Just yielding doesn't give us much breathing room, the CPU keeps spinning.
					usleep(500);
#else
					sched_yield();
#endif
				}
			}
			else
				cbl::log_verbose("Waiting for pid %d failed; wstatus : %X, reason: %s", pid, wstatus, strerror(error));
		}
	}
}

#endif	// defined(__linux__)
