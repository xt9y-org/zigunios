# HorseEngine.cpp and Zig / c build-system integration

This document is an audit of the actual upstream sources:

- [HorseEngine.cpp/setup.sh](https://github.com/xt9y/HorseEngine.cpp/blob/main/setup.sh)
- [HorseEngine.cpp/build.c](https://github.com/xt9y/HorseEngine.cpp/blob/main/build.c)
- [xt9y/build.c](https://github.com/xt9y/build.c), especially `src/cli.c` and `src/main.c`.

## What you can run inside Zigunios now

```sh
cd /home/projects
git clone https://github.com/xt9y/HorseEngine.cpp.git
cd HorseEngine.cpp
project inspect
c inspect
setup check
setup deps
```

`setup check` reads setup.sh, listing recognized Git actions without downloading.
`setup deps` recognizes Git clone commands and `git -C DIR submodule update --init --recursive` from the actual script. It fetches repositories into the workspace; it **does not execute Bash or CMake**. It never claims to have built the dependencies. It recognizes a narrow, explicit command grammar, not arbitrary shell-language substitutions, loops or conditionals.

Also supported as direct Git commands:
```sh
git clone --depth 1 --branch v0.11.0 --recurse-submodules https://github.com/Bigfoot71/r3d.git deps/r3d
git clone --depth 1 --branch v0.1.0 https://github.com/erincatto/box3d.git deps/box3d
git clone --depth 1 --branch v5.5.0 --recurse-submodules https://github.com/RobLoach/raylib-nuklear.git deps/raylib-nuklear
git -C deps/r3d submodule update --init --recursive
git -C deps/raylib-nuklear submodule update --init --recursive
```

These use **libgit2**, not a native Git executable. The large R3D/Assimp repository chain can require substantial time and storage. HTTP public repositories are supported; Git credentials, SSH and arbitrary Git options are not yet available. The option parser supports the flags used in HorseEngine's setup.sh.

## What setup.sh actually requires

| Operation | Upstream pinned version / command | iPad Zigunios |
|---|---|---|
| Clone R3D | `Bigfoot71/r3d v0.11.0` | Git fetch implemented |
| R3D submodules | Raylib and Assimp pinned by R3D | Git submodule update implemented |
| Clone Box3D | `erincatto/box3d v0.1.0` | Git fetch implemented |
| Clone raylib-nuklear | `RobLoach/raylib-nuklear v5.5.0` | Git fetch implemented |
| Nuklear submodule | upstream pinned revision | Git submodule update implemented |
| Configure R3D | `cmake -S deps/r3d -B deps/r3d/build ...` | **Not available** |
| Compile R3D/Raylib/Assimp | `cmake --build ... --target r3d raylib assimp` | **Not available** |
| Configure/build Box3D | CMake native targets | **Not available** |
| Run Horse build | `c build run -- [model.glb]` | **Not available** |

`setup deps` prepares the **source dependency tree only**, not the `.a` archives that setup.sh normally produces. If a Git operation fails, check the output instead of assuming the workspace is fully populated.

## Why your current c binary cannot simply run inside an IPA

The real `xt9y/build.c` is not an ordinary parser for `build.c`. It:

1. Compiles a C build description to an executable host **shared library** (`.dylib` on macOS).
2. Dynamically loads it (`dlopen`) and calls `build(C_Build*)`.
3. Uses POSIX `fork` / `execvp` / `posix_spawnp` and waits for compiler, linker, Git, CMake and program processes.

The current iPadOS app cannot execute freshly generated unsigned native Mach-O code or freely spawn Unix processes. Shipping the existing `c` binary unmodified will not solve that limitation.

An iPad-compatible `c` runner must introduce an **in-process compiler + interpreted build-description runtime + task scheduler** that does not depend on `dlopen`, `fork` or `posix_spawnp`. The existing `cbuild.h` source API should remain compatible.

## Zig CC plan

**Zig is a build/compiler frontend, not a kernel replacement.** `zig cc` and `zig c++` use Clang/LLVM components. A regular macOS or Linux Zig binary is not an iPadOS executable, and merely downloading one from a repository does not make it runnable in the iPad app.

Practical implementation stages:

1. Port or bundle a native **signed Clang/LLVM compiler frontend** that can emit `wasm32-wasi`. [a-Shell](https://github.com/holzschu/a-shell) demonstrates on-device C/C++ to WASM compilation; its licenses and frameworks need separate integration work.
2. Add a WASI sysroot, linker (`wasm-ld`) and archive support to produce linked programs without spawning another executable.
3. Expose compiler commands through Zigunios terminal; **do not label the command `zig cc` unless it uses a real Zig-supported toolchain**, not an unrelated compiler renamed to Zig.
4. Adapt the `c` build-description compiler to WASI and inspect its exported build graph through stable host callbacks. Replace per-process compiler jobs with in-process invocations and run program artifacts using wasm3 or another interpreter.
5. Add native iPad graphics integration for supported targets. A Metal triangle proving a display surface exists does **not** mean a desktop OpenGL engine will run.

The *current implementation has none of these compiler stages*. `zig`, `c build`, `cmake` and `make` must continue reporting unavailable, not silently pretend to compile.

## Horse graphics problem is independent

The Horse build config links `Cocoa`, `OpenGL`, `IOKit`, desktop audio frameworks and static libraries built for a host desktop toolchain. R3D v0.11.0 additionally requires **desktop OpenGL 3.3 or later**, including texture-cube-map-array support.

Even when WASM C++ compilation is implemented, the desktop R3D game **cannot run unchanged in an iPad WASI interpreter**. A renderer/runtime port or an OpenGL-to-Metal translation layer is a separate project; modern OpenGL support was explicitly deferred for Zigunios.

## Device verification

In the v0.4 app, test the Git/source-fetch stage with `setup check`, then `setup deps`. The above Git commands and inspector are implemented and the iOS app is built in GitHub Actions, but **actual clone/submodule runtime behavior on your physical iPad has not been independently confirmed yet**. The existing `wasm demo` and `metal demo` have been reported working on the device.

Avoid deleting the app before installing an updated IPA if the project files must remain intact.
