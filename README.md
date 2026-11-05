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

## Features

**Boot and CPU**
- BIOS boot sector that enters 32-bit protected mode and starts the kernel.
- IDT with safe-halting CPU exception handlers; PIT timer, keyboard (IRQ1)
  and PS/2 mouse (IRQ12) interrupts.
- x86 paging with a 4 KiB physical page allocator and virtual mapping APIs
  (the first 16 MiB is identity-mapped).

**Processes**
- Preemptive round-robin scheduler that saves and restores full register,
  segment and interrupt contexts. Processes run in ring 0 in the kernel
  address space; they are not privilege-isolated.
- The kernel starts the shell as an ordinary program (`SHELL.EXE`) and
  restarts it if it exits. The shell parses commands and uses the
  `start_process` and `wait_process` services to run other programs.
- Foreground and background (`&`) processes, one pipe between programs
  (`a | b`), `kill`, and child-process creation and waiting.
- System calls through `int 0x80`: print, exit, keyboard scancode polling,
  process, filesystem, time, window and graphics services.

**Graphics and desktop**
- 1280x720 VBE framebuffer with a 16-color palette.
- Desktop with a light-grey wallpaper, a fixed non-closable shell window and a
  taskbar showing every window in fixed-width, left-aligned items (titles are
  truncated to 15 characters).
- Per-process window handles: programs create windows and draw into them, and
  a process without a window cannot enter graphics mode.
- PS/2 mouse pointer; left-click to raise a window or select it from the
  taskbar, and drag windows by their title bars. Rendering is double-buffered
  to avoid flicker.

**Storage**
- ext2 filesystem (1 KiB blocks) with directory listing, file reads and
  writes within preallocated capacity, and directory creation.
- Two disks: the system disk (`A:`, `build\disks\system.img`) and an 8 MiB
  persistent data disk (`B:`, `build\disks\data.img`). Drive-letter paths and
  relative paths with `.` and `..` are supported.
- Real-time clock access through the CMOS.

**Executables and toolchain**
- `MEXE` format: a 12-byte header (`MEXE`, 32-bit image size, 32-bit entry
  offset) followed by flat, relocatable 32-bit code; images up to 64 KiB are
  loaded from the ext2 root directory.
- Programs under `src\programs\` are built by `build.py` using two compilers:
  - **High** (`*.high`, `tools\high_compiler.py`): a small procedural language.
    See [docs/high-language.md](docs/high-language.md).
  - **minc** (`*.c`, `tools\minc.py`): a minimal C compiler that preprocesses
    `#include`, flag `#define` and `#ifndef/#ifdef/#endif`, generates NASM
    assembly and links it with the runtime in `src\programs\runtime\`
    (`crtl.asm` is the C runtime: entry point, `printf`, `strcmp`,
    `starts_with`; `minos.asm` has MinOS helpers such as `read_line` and
    `parse_int`). Headers live in `src\programs\include\`. The supported
    subset covers `int`/`char`/`void`/`float`, pointers, local arrays and
    structs, `if`/`while`/`for`/`switch`, compound assignment and
    `++`/`--`.

**Shell**
- Built-in `echo`; every other command is a program in the image, run by
  name with or without `.EXE`.
- Command-line arguments, `&` for background jobs and a single `|` pipeline.

## Tools and layout

- `src\` holds the OS: `kernel.c`, `video.c`, `keyboard.c`, `mouse.c`,
  `interrupts.c`, `time.c`, `memory.c`, `process.c`, `filesystem*.c`,
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
