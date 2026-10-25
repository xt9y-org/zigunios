# Zigunios native C compiler (experimental)

Zigunios v0.5 adds a **real C compiler embedded into the signed iPad application**. This is deliberately not a fake `zig cc` binary. It uses the iOS Clang/LLD 14 frameworks published for the open-source a-Shell application, alongside an older WASI SDK 20 sysroot, and compiles into `wasm32-wasi` WebAssembly. The existing wasm3 runtime interprets resulting `.wasm` modules.

## First device test

```sh
cc --doctor
cc --version
cd /home/projects
mkdir compiler-demo
cd compiler-demo
c init
c build run
```

This creates a real `build.c` using the `xt9y/build.c` API and a one-file C application. The native Clang bridge compiles an intermediate `build.c` evaluator to WebAssembly, executes it inside wasm3 to discover the target, then compiles and runs the sample C file.

**Note:** only the iOS Xcode/IPA build is currently confirmed. The compiler framework's runtime loading, Clang resource headers, WASI output, cbuild evaluation and subsequent program execution have **not** yet been verified on an iPad. If `cc --version` returns an error, capture it before trying `c build run`.

You can also compile a single preexisting C file:

```sh
cc hello.c
wasm hello.wasm
```

Use `cc hello.c -o myprogram.wasm` to choose the output name. The initial compiler handles one `.c` file at a time. The `c` build adapter handles a single default executable target with one directly named C file, no external static libraries and no wildcard expansions.

## How it works

```
UIKit terminal
   |
   +-- c init
   |    \-- writes build.c, main.c
   |
   +-- cc main.c
   |    \-- iOS command bridge -> prebundled Clang 14
   |        \-- wasm32-wasi .o
   |            \-- in-process wasm-ld + wasi-libc -> main.wasm
   |                \-- wasm3 interpreter -> stdout + exit code
   |
   +-- c build run
        \-- compile generated C wrapper that includes original build.c
            \-- execute wrapper in wasm3, invoking real build(C_Build *)
                \-- read cbuild target, source, flags
                    \-- compile supported single-C-file target
                        \-- execute target with wasm3
```

The external `clang` and `wasm-ld` command implementations are native iOS frameworks with signed machine code **bundled before installation**, not downloaded or executed by the iPad app. The generated code is WebAssembly interpreted in-process; there is no JIT or dynamic loading of newly compiled native code. a-Shell's `ios_system` supports in-process command dispatch without ordinary subprocess spawning. We use its command dictionary and avoid unrestricted shell execution.

The compiler components are assembled by [`scripts/prepare-compiler-ios.sh`](../scripts/prepare-compiler-ios.sh). The unsigned MobAI-based GitHub Actions IPA includes embedded `clang.framework`, `libLLVM.framework`, `lld.framework`, `ios_system.framework`, Clang's builtin headers and the WASI 20 sysroot. Signing must include **all four nested frameworks**, or iPadOS will refuse to load them.

Dependencies:
- `holzschu/llvm-project` iOS Clang, LLD, libLLVM 14.0.0 (LLVM project's Apache-2.0 WITH LLVM-exception)
- `holzschu/ios_system` 3.0.5 (BSD-3-Clause)
- `WebAssembly/wasi-sdk` 20 (Apache-2.0 WITH LLVM-exception for LLVM portions, wasi-libc license files)
- `xt9y/build.c` header pinned to `1d4c6762ba8fb06b726ebd16101264f2f5206aa0`
- `wasm3` 0.9.0 (MIT), already integrated

## Unsupported behaviors

The original `xt9y/build.c` desktop implementation builds a dynamic library from `build.c`, loads it with `dlopen`, and starts child compiler processes. Zigunios does **not** attempt these disallowed iPadOS operations. Its interpreted build-description evaluator can execute ordinary C build graph logic within WASI but cannot automatically support `c_generate` host shell commands, native linked archives, arbitrary target dependency graphs, parallel compilation, custom compiler flags, linked native frameworks, or existing Mach-O/ELF executables.

HorseEngine.cpp's `setup.sh` additionally requires Bash, CMake, C++17, R3D, Raylib, Assimp and Box3D. Zigunios can currently **fetch** the pinned dependencies with `setup deps`; it cannot compile and run that project unchanged. Even full C++ compilation would not resolve its desktop OpenGL dependency.

This is a correctness-first step toward the requested workflow, not full build-system compatibility.

## Diagnostics

- `cc: iPad compiler framework missing` — check the IPA includes nested frameworks and that they were signed.
- `cc: unable to load LLVM/ios_system` — dynamic library load/signature/runtime dependency failure.
- `cc: WASI sysroot missing` — IPA packaging issue.
- `cc: command returned ...` — the native iOS command invocation failed; copy its stderr.
- `c: unsupported target` — a build description executed, but the generated target uses more than the current C-only subset.
- `wasm ...` — inspect the precise WASI exit code and stdout.

**Do not delete your existing Zigunios app before reinstalling** unless you have copied its workspace data elsewhere; deleting the app may remove its Documents sandbox.
