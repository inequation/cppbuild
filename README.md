# cppbuild

A toy self-hosting build system for C++, written in C++14.

Inspired in equal parts by the frustration with existing C++ build systems, as well as conversations with colleagues at work.

I don't expect anyone to take "frustration with the build system" as valid justification at face value for developing Yet Another One, so I've written a little bit about that [in the project's wiki](https://github.com/inequation/cppbuild/wiki/Rationale).

# Features

1. **Same language** as your actual codebase.
2. C++'s **portability, power of expression** and **debuggability**.
3. [**Self-hosting**](https://github.com/inequation/cppbuild#self-hosting) with **trivial bootstrapping**. Subsequent self-rebuilds are seamless and automatic.
4. **Perfect dependency tracking** using the compilers' include tracking mechanisms (`-M`/`/showIncludes`).
5. [Minimal **library**](cbl.h) for exploring and manipulating the build environment: logging, time, file system, processes, etc.
6. **Parallel execution** via tasks, based on [enkiTS](https://github.com/dougbinks/enkiTS).
7. **Profiling** of every single build, based on [minitrace](https://github.com/hrydgard/minitrace).

# Self-hosting

C++ compiles to native executables. It stands to reason that in order to achieve reasonable usability, bootstrapping the build system executable must be a trivial step.

Therefore, all it takes to bootstrap a first-generation `build`/`build.exe` binary is a trivial compiler invocation, such as this, on Linux with GCC:
```
$ g++ -o build -pthread build.cpp && ./build
```
Or this, on Windows, in a Visual Studio Developer Command Prompt:
```
cl.exe build.cpp && build.exe
```
The invocation of `build`/`build.exe` will then build a second-generation *cppbuild* binary that has all the bells and whistles.

After that, *cppbuild* will rebuild itself automatically and seamlessly as needed, when changes to your `build.cpp` file, or to *cppbuild* itself, are detected.

# Getting started

I'm sorry, you've showed up a bit early! I've got an example project cooking up, driving the requirements for this, and I'll publish it on GitHub once it's presentable, so hit that star button if you want to follow along.

However, to give an idea of what I have in my mind - the mock steps would be as follows:
1. Open a shell in your project's directory and `git submodule add git@github.com:inequation/cppbuild.git`.
2. Copy `cppbuild/build.example.cpp` to your project directory and rename it to `build.cpp`. It's the functional equivalent of a makefile in *cppbuild*.
3. Customize the `describe()` function body inside that file.
4. Bootstrap *cppbuild* using one of the command lines:
   `$ g++ -o build build.cpp && ./build` on Unices, or
   `cl.exe /Febuild.exe /Zi build.cpp && build.exe` on Windows.
5. Get to work on your project. Edit `build.cpp` to your heart's content. Run `build <target> [options]` to build your project. *cppbuild* will rebuild itself automatically and seamlessly as needed.
