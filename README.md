# Zigunios

A small iPad development-workstation experiment: native UIKit terminal, persistent Unix-style workspace, and a future C/C++ compiler and in-app program runner. **Not an iOS replacement, Linux kernel, or working C compiler yet.**

## What works in v0.1

- Native, dark iPad terminal UI (Objective-C/UIKit), hardware/software keyboard input.
- Portable C11 shell core with `pwd`, `cd`, `ls`, `mkdir`, `touch`, `cat`, `echo`, `rm`, `rmdir`, `clear`, `uname`, `whoami`, `date`, `help`.
- Basic quoting and `echo text > file` / `>>` redirection.
- Persistent sandbox rooted at `/` (mapped to app `Documents/root`), starting directory `/home` and project folder `/home/projects`.
- Files app sharing and Import files button (imports to `/home/projects`).
- iPadOS 27-ready `UIScene` lifecycle and launch storyboard.

**Not implemented:** `git clone`, Zig compiler, `c build run`, Make/CMake, arbitrary process execution, OpenGL/Metal game execution, POSIX-compatible pipelines and job control. These are deliberately tracked as future milestones rather than mocked as working commands.

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

Current code is a tested portable shell prototype and an unverified iOS Xcode project: Xcode/device compilation cannot be run from this non-macOS environment. Please report the first Xcode build error if any.
