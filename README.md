# cppbuild

A toy self-hosting build system for C++, written in C++11.

Inspired in equal parts by the frustration with existing C++ build systems, as well as conversations with colleagues at work.

# Rationale

"Frustration with the build system" is not valid justification for developing Yet Another One, by my book. So why am I writing one?

1. **Portability.** I wanted something that would work with any IDE on any platform that I'm hosting the toolchain on. I usually make do with makefiles (pardon the pun), but compatibility between *GNU make* and Microsoft's *NMAKE* really limit your options to the bare basics.
2. **Same language as the codebase.** CMake's syntax, for instance, I find repulsive. *ANT* and *NANT* require Java and C# skills to maintain, respectively, drawing a fine line between the codebase substance and the build process meta. With *cppbuild*, when working on a C++ codebase, your skills would be directly transferable to the build system, and the entire force of expression of C++ is at your disposal to customise your build.
3. **Mature and extensive tooling.** Attach your favourite debugger and inspect WTF is going on with the compiler flags to your heart's content.
4. **Generated code as a first class citizen.** Not as a convoluted, heavily constrained pre-build step that emits a script in a third language, that is then executed in series, that has ordering issues... Have the build graph support the idea from the get go instead.
5. **Fun.** Programmer excuse #1. There are some pretty cool (parallel) graph processing opportunities here that I'm looking forward to.

Will it be any faster than the existing systems? Or even reliable? Heck if I know. So far, I'm just enjoying myself.

# Self-hosting build system

C++ compiles to native executables. It stands to reason that in order to achieve reasonable usability, bootstrapping the build system executable must be a trivial step. Once that is complete, we can use the full power of expression of C++ to self-host ourselves.

Therefore, it is my goal to be able to bootstrap a first-generation `cppbuild`/`cppbuild.exe` binary with a trivial compiler invocation, such as this:
```
$ g++ -o cppbuild cppbuild.cpp && ./cppbuild
```
Or this, on Windows, in a Visual Studio Developer Command Prompt:
```
cl.exe cppbuild.cpp && cppbuild.exe
```
The invocation of `cppbuild`/`cppbuild.exe` will then build a second-generation *cppbuild* binary that has all the bells and whistles.

# Getting started

I'm sorry, you've showed up a bit early! I've got an example project cooking up, driving the requirements for this, and I'll publish it on GitHub once it's presentable, so hit that star button if you want to follow along.

However, to give an idea of what I have in my mind - the mock steps would be as follows:
1. Open a shell in your project's directory and `git submodule add git@github.com:inequation/cppbuild.git`.
2. Copy `cppbuild/cppbuild.example.cpp` to your project directory and rename it to `cppbuild.cpp`. It's the functional equivalent of a makefile in *cppbuild*.
3. Customize the `describe()` function body inside that file.
4. Bootstrap *cppbuild* using one of the command lines:
   `$ g++ -o cppbuild cppbuild.cpp && ./cppbuild` on Unices, or
   `cl.exe /Fecppbuild.exe /Zi cppbuild.cpp && cppbuild.exe` on Windows.
5. Get to work on your project. Edit `cppbuild.cpp` to your heart's content. Run `cppbuild <target> [options]` to build your project. *cppbuild* will rebuild itself automatically and seamlessly as needed.
