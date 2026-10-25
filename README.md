# Zigunios

A small iPad development-workstation experiment: native UIKit terminal, persistent Unix-style workspace, and a future C/C++ compiler and in-app program runner. **Not an iOS replacement, Linux kernel, or working C compiler yet.**

## Current development build (v0.4)

- UIKit iPad terminal with keyboard, import, and a persistent Unix-style workspace.
- Shell commands: `pwd`, `cd`, `ls`, `mkdir`, `touch`, `cat`, `cp`, `mv`, `rm`, `rmdir`, `grep`, `head`, `wc`, `echo`, `clear`, `uname`, `whoami`, `date`, `help`.
- Real Git over HTTPS with statically linked **libgit2**: `git clone`, `git status`, `git log`, `git branch`, and `git fetch`. This creates normal Git repositories with a real `.git` directory, not extracted ZIP archives.
- Git supports pinned clone options (`--depth 1`, `--branch TAG`, `--recurse-submodules`) and `git -C DIR submodule update --init --recursive` for HorseEngine's setup.sh.
- `setup check` inspects a local setup.sh script, and `setup deps` fetches its recognized Git dependencies **without running Bash, CMake or Clang**.
- `project inspect` / `c inspect` analyzes setup.sh and build.c compatibility (source inspection, not compilation).
- Git networking and filesystem commands run off the main UI thread.
- `edit filename` opens a native UTF-8 source editor with Save/Cancel.
- `wasm demo` runs a bundled example WebAssembly module; `wasm file.wasm` runs imported WASI Preview 1 modules through the **wasm3 interpreter** (no JIT).
- `metal demo` opens a native GPU-rendered triangle inside the same iPad app and returns to the terminal with Done.
- Private repos requiring authentication, Git SSH, Git push/pull, pipelines, POSIX process spawning, Zig/CMake/Make, arbitrary native executables, and compiling/running graphics projects **are not implemented**. Local `git add` / `git commit` work with configured repository identity.

Try a public project:

```sh
cd /home/projects
git clone https://github.com/libgit2/clar.git
cd clar
git status
git log
git config user.name "Your Name"
git config user.email "you@example.com"
edit notes.txt
wasm demo
metal demo
ls
```

The `c build`, `zig`, `cmake` and `make` commands currently return **not implemented**. The real `xt9y/build.c` needs executable build modules and subprocess support; the next substantial stage is an in-process compiler and build graph runtime. WASM3 executes modules but is not a compiler. See [HorseEngine.cpp integration audit](docs/HORSE.md). iPadOS doesn't allow executing newly compiled unsigned ARM64 binaries; implementing compilation requires a compiler linked into the signed host app and an interpreted runtime (such as WebAssembly). See [the architecture](docs/ARCHITECTURE.md).

### Build requirements

The GitHub Actions workflow automatically downloads and builds **libgit2 v1.9.7** (Apple SecureTransport) and **wasm3 v0.9.0** for iOS ARM64. For a local Xcode build, run `bash scripts/build-libgit2-ios.sh` and `bash scripts/build-wasm3-ios.sh` first (CMake + Ninja). No prebuilt binary dependency is checked into this repository.

## HorseEngine.cpp source fetch

```sh
cd /home/projects
git clone https://github.com/xt9y/HorseEngine.cpp.git
cd HorseEngine.cpp
project inspect
setup check
setup deps
```

This downloads pinned source repositories, but **does not build R3D or start a game**. For the actual setup.sh commands, the `c` build-system ABI, the native Zig CC limitations and the eventual app architecture, read [docs/HORSE.md](docs/HORSE.md).

## Download an IPA from GitHub Actions

Zigunios has an [iOS Build workflow](https://github.com/xt9y-org/zigunios/actions/workflows/ios-build.yml) based on [MobAI ios-builder](https://github.com/MobAI-App/ios-builder). It builds an **unsigned IPA** on GitHub-hosted macOS on every push to `main`, or when you click **Run workflow**. After a successful run, download the **ipa** artifact under **Artifacts** and extract the IPA.

To install on your iPad without the $99 Apple Developer membership, **sign the unsigned IPA using a free Apple Account** through SideStore, AltStore, Sideloadly, or configured MobAI. It cannot be installed unsigned. Free-account signatures generally expire after seven days. See [full IPA build and sideloading steps](docs/IPA.md).

## Try it on your iPad for free

Follow [docs/DEVICE.md](docs/DEVICE.md). No paid Apple Developer membership required: Xcode's **Personal Team** signs the app to your own iPad for 7 days, then you rebuild/reinstall.

```sh
git clone https://github.com/xt9y-org/zigunios.git
cd zigunios
open Zigunios.xcodeproj
```

In Xcode, select your Personal Team under **Zigunios → Signing & Capabilities**, connect your iPad, choose it as the run destination, and press Run.

Then in the iPad terminal:

```sh
pwd
ls
mkdir demo
cd demo
echo hello from zigunios > hello.txt
cat hello.txt
```

## Local shell testing on macOS/Linux

```sh
cc -std=c11 -Wall -Wextra -Icore core/shell.c tools/shell_cli.c -o shell-cli
./shell-cli ./local-root
```

The portable CLI helps debug filesystem behavior without installing the iPad app. To use Zig as a **C compiler** on your Mac:

```sh
zig cc -std=c11 -Icore core/shell.c tools/shell_cli.c -o shell-cli
```

## Architecture

See [the mind map and milestones](docs/ARCHITECTURE.md). The host OS is iPadOS. Zig is proposed as a future bundled compiler frontend/toolchain, **not a custom kernel or required runtime**. Main graphical and shell UI uses Objective-C; the shell core is portable C.

The iOS GitHub Actions build is the source of truth for device compilation. This development version has not yet been tested on a physical iPad after the Git integration.
