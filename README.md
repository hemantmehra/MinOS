# MinOS

A tiny 32-bit x86 operating system. MSVC builds the freestanding C kernel,
NASM assembles a BIOS boot sector, and QEMU runs the resulting disk image.

MSVC produces Windows PE executables rather than the Multiboot ELF files
accepted by QEMU's x86 `-kernel` option. The NASM BIOS boot sector loads the
linked kernel's code and data from disk, switches the CPU to 32-bit protected
mode, and jumps to the kernel. This avoids needing a separate bootloader
project such as GRUB.

## Requirements

- Visual Studio with the MSVC x86 C/C++ build tools
- NASM
- QEMU with `qemu-system-i386`
- Python 3

## Build and run

```text
python build.py
python run.py
```

QEMU appends structured kernel logs from the debug console to
`build\diagnostics\kernel.log`, preserved across runs.

### Clang ELF programs

The optional ELF toolchain uses a [prebuilt Windows LLVM distribution](https://releases.llvm.org/download.html) containing
`clang.exe` and `ld.lld.exe`. Add its `bin` directory to `PATH`, or set
`LLVM_BIN` to that directory for the current PowerShell session:

```powershell
$env:LLVM_BIN = 'C:\path\to\LLVM\bin'
python build.py --elf
python run.py
```

In MinOS, run `hello world` to execute the Clang-built example. The `--elf`
option builds `src\elf_programs\*.c` and installs them alongside the existing
`minc` programs. The `.EXE` extension is used for shell lookup; the contents
are ELF. To build a program separately:

```powershell
python tools\build_elf.py src\elf_programs\hello.c -o build\programs\HELLO.EXE
```

Pass multiple `.c` files before `-o` to link them into one program. The build
also discovers LLVM extracted under `%LOCALAPPDATA%\MinOS\llvm-*\files\LLVM\bin`.
ELF programs use a separate small C library in `src\elf`. It supplies byte
string and memory functions, `malloc`/`calloc`/`realloc`/`free`, integer
conversion, `errno`/`strerror`/`perror`, and unbuffered standard and file
streams. `printf` and `fprintf` support `%d`, `%i`, `%u`, `%x`, `%c`, `%s`,
and `%%`; width, precision and floating-point formats are not supported.
The growable per-process heap is limited to 16 MiB and pages are released
when the process exits.

`fopen` with `w` or `a` creates a missing regular file on `B:`; `w`
truncates an existing file and `a` appends to one. Writing and creation are
supported only on `B:`. Seeking beyond EOF is allowed; a subsequent write
zero-fills the gap within the 12 KiB write limit.
File-open failures report `EIO` because the kernel
does not yet distinguish missing files from other open errors. Console input
has no EOF key; pipe input detects a closed producer. File growth is limited
to 12 KiB, and simultaneous open
streams of the same file do not provide coherent concurrent writes or atomic
append. This is not a complete ISO C or POSIX library.

ELF programs can use `opendir`, `readdir`, `closedir`, `mkdir`, `chdir`,
and `getcwd`. `struct dirent` currently exposes only `d_name`; `mkdir`
ignores its mode and uses fixed permissions. Directory names are limited to
one to eight letters, digits, underscores or hyphens. The current directory
is shared globally, rather than per process. The filesystem is not journaled;
I/O errors or interruption during metadata updates can require repair.

Each process has a 1 MiB stack and a growable heap. Both ELF and minc C
programs receive `argc`, a null-terminated `argv` with the executable path in
`argv[0]` (the path passed to the loader), and a null-terminated `envp` as a
third `main` argument. ELF
programs can also use `getenv` and `environ`. The shell supports `set
NAME=value` and `set` to list variables; `PATH` controls program lookup.
Children inherit a copy of the parent's environment (up to 16 entries, each
at most 127 characters). ELF `environ` is a startup snapshot, not a mutable
kernel environment; there is no `setenv` or `unsetenv` yet. Arguments are
space-separated with no quoting or escaping and the command line is limited
to 127 characters. `wait_process` returns an 8-bit child exit status, or `-1`
on failure; background processes are not waitable.
These are runtime-provided arguments, not a Linux-compatible initial stack ABI.

Run `python tests\test_elf_format.py` to test ELF header validation. The
`tests\libc_test.c` and `tests\libc_pipe.c` ELF programs exercise the C library;
run the former only against a disposable data disk because it overwrites
`B:\WRITE.TXT`.
`tests\filesystem_services.c` creates files and directories and must likewise
be run only on a disposable disk; running it with an argument verifies persistence
after reboot.

### Local HTTP service

The kernel starts `HTTPSRV.EXE` in the background and retries starting it
after it exits. Run `httpget` in the shell to send `GET /` to
`localhost:8000` and display the HTTP response, whose body is `Hello World`.
The server accepts HTTP/1.0 or HTTP/1.1 GET requests for `/`, replies with
400 for other or incomplete requests, and times out an incomplete request
after five seconds. It handles clients sequentially.

These are **MinOS-local byte streams**, not TCP/IP: there is no NIC, DNS,
external network access, host access, or POSIX socket compatibility. The
minc runtime exposes `socket`, `socket_listen`, `socket_connect`,
`socket_accept`, `socket_send`, `socket_recv`, and `socket_close`. There are
32 endpoints total (a connection uses two), a 1024-byte receive buffer per
endpoint, and at most 256 integer-array byte values per send/receive call.
Calls are nonblocking: `-2` means retry, `-1` means error, and receive
returns zero when the peer has closed and buffered data has been drained.
Handles belong to their creating process and are closed automatically on
exit. See [process system calls](docs/process.md#system-calls) for details.
`tests\loopback_test.c` exercises endpoint lifecycle and buffering on the
host; `tests\http_socket.c` is a minc guest test for HTTP framing,
fragmentation, invalid requests and timeout recovery.

## Features

**Boot and CPU**
- BIOS boot sector that enters 32-bit protected mode and starts the kernel.
- IDT with safe-halting CPU exception handlers; PIT timer, keyboard (IRQ1)
  and PS/2 mouse (IRQ12) interrupts.
- x86 paging with a 4 KiB physical page allocator and virtual mapping APIs
  (the first 512 MiB of RAM is identity-mapped; QEMU is run with `-m 512M`).
  Up to 32 graphics windows can be open at once; the taskbar shrinks its
  items to fit.

**Processes**
- Preemptive round-robin scheduler that saves and restores full register,
  segment and interrupt contexts. Every program runs in ring 3 in its own
  address space (a private page directory sharing only the kernel mappings,
  which user code cannot access); a fault terminates just that program.
- The process table grows on demand, so the number of processes is limited
  only by free memory.
- The kernel starts the shell as an ordinary program (`SHELL.EXE`) and
  restarts it if it exits. The shell parses commands and uses the
  `start_process` and `wait_process` services to run other programs.
- The kernel also supervises the user-mode `MINDESK.EXE` desktop server.
  Window creation requests and mouse events are handled by Mindesk; the
  kernel retains framebuffer composition, window buffers, and keyboard
  delivery. Existing windows survive a Mindesk restart. A pending window
  request fails if the server exits, and new requests fail until it restarts.
  `tests\desktop_client.c` exercises server-call authorization and window
  creation. The `tests\desktop_wait_*` programs and
  `tests\desktop_sleep_server.c` test cancellation using a deliberately
  nonresponsive server on an isolated test image.
- Foreground and background (`&`) processes, one pipe between programs
  (`a | b`), `kill`, and child-process creation and waiting.
- System calls through `int 0x80`: print, exit, keyboard scancode polling,
  process, filesystem, time, window and graphics services.

**Graphics and desktop**
- 1280x720 VBE framebuffer with a 16-color palette.
- Desktop with a light-grey wallpaper, a fixed non-closable shell window and a
  taskbar showing every window in fixed-width, left-aligned items (titles are
  truncated to 15 characters).
- A **Start** button at the bottom left of the taskbar opens a menu with
  `shell` (focus the shell window) and `ball` (start the bouncing-ball
  program in a new window). Program windows have a red close button at the
  right of the title bar that ends the program.
- Per-process window handles: programs create windows and draw into them, and
  a process without a window cannot enter graphics mode.
- Run `breakout` for a brick-breaking game. Use the left and right arrow keys
  to move the paddle, earn 10 points per brick, and use `R` to restart after
  winning or losing. Escape or the window close button exits. Run it in the
  foreground: background processes do not receive keyboard input.
- PS/2 mouse pointer; left-click to raise a window or select it from the
  taskbar, and drag windows by their title bars. Rendering is double-buffered
  to avoid flicker.

**Storage**
- ext2 filesystem (1 KiB blocks) with directory listing, file and directory
  creation on `B:`, and file writes up to 12 direct blocks.
- `minedit <file>`: command-line text editor (Ctrl+W writes, Ctrl+X exits,
  status line at the bottom). It edits existing `B:` files up to 1000
  characters.
- Two disks: the system disk (`A:`, `build\disks\system.img`) and an 8 MiB
  persistent data disk (`B:`, `build\disks\data.img`). Drive-letter paths and
  relative paths with `.` and `..` are supported.
- Real-time clock access through the CMOS.

**Executables and toolchain**
- `MEXE` format: a 12-byte header (`MEXE`, 32-bit image size, 32-bit entry
  offset) followed by flat, relocatable 32-bit code; images up to 4 MiB are
  loaded from the ext2 image (`build.py` installs them in `A:/Programs`).
- Statically linked i386 ELF32 executables with non-overlapping load segments
  inside `0x40000000`–`0x403FFFFF`. Files and loaded images are each limited
  to 4 MiB; the loader zeros BSS and rejects interpreters, TLS, and dynamic linking.
  ELF pages are currently writable and executable.
- All programs under `src\programs\*.c` are built by `build.py` with
  **minc** (`tools\minc.py`), a minimal C compiler that preprocesses
  `#include`, flag `#define` and `#ifndef/#ifdef/#endif`, generates NASM
  assembly and links it with the runtime in `src\programs\runtime\`
  (`crtl.asm` is the C runtime: entry point, `printf`, `putchar`, `strcmp`,
  `starts_with`; `minos.asm` wraps the MinOS system calls and provides
  `read_line` and `parse_int`). Headers live in `src\programs\include\`. The
  supported subset covers `int`/`char`/`void`/`float`, pointers, local arrays
  and structs, `if`/`while`/`for`/`switch`, compound assignment and
  `++`/`--`.

**Shell**
- Built-in `echo` and `set`; every other command is a program, run by name
  with or without `.EXE`.
- Programs are installed in `A:/Programs`. The shell searches the current
  directory first, then each directory of its `PATH` variable (default
  `A:/Programs`, `;`-separated, left to right). Names containing `/` or `:`
  are used as given. `set` prints `PATH`; `set PATH=dir;dir` changes it.
- Command-line arguments, `&` for background jobs and a single `|` pipeline.

## Tools and layout

- `src\` holds the OS: `kernel.c`, `video.c`, `keyboard.c`, `mouse.c`,
  `interrupts.c`, `time.c`, `memory.c`, `process*.c`, `filesystem*.c`,
  `shell_utils.c` and `boot.asm`; interfaces are in the matching headers.
- `python tools\read_disk.py [image [path]]` lists or reads files on any
  supported ext2 image while MinOS is shut down.
- `python tools\make_data_disk.py` recreates the data disk from
  `tools\data_files\`, which resets its contents.
- Build outputs go to `build\boot`, `build\kernel`, `build\programs`,
  `build\disks` and `build\diagnostics`. Rebuilding recreates the system disk
  but preserves an existing data disk.
- MSVC links the kernel at a fixed 32-bit address; kernel code must stay
  small and freestanding, without Windows APIs, a C runtime, SIMD or
  floating-point state.
