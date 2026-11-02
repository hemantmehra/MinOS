# MinOS

A tiny 32-bit x86 kernel with an interactive keyboard shell. MSVC builds the
freestanding C kernel, NASM assembles a BIOS boot sector, and QEMU runs the
resulting disk image.

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

From a terminal in this directory:

```text
python build.py
python run.py
```

QEMU opens the kernel with a BIOS-backed PC. The kernel uses 640x480 VGA
graphics with a 16-color palette, rendering the shell with bitmap characters.
It prints the greeting and displays a keyboard-driven `minios>` prompt. The
disk image contains a writable FAT16 filesystem populated with project files
and directories.

Kernel code is organized by subsystem: `kernel.c` handles shell dispatch,
`video.c` renders the VGA terminal, `keyboard.c` reads keyboard input,
`filesystem.c` implements FAT16 access, `basic.c` contains the BASIC
interpreter, `interrupts.c` sets up the IDT and keyboard IRQ, `time.c` reads
the CMOS real-time clock, and `shell_utils.c` provides shared shell helpers.
CPU exceptions halt safely, keyboard input is delivered through IRQ1, and
shared interfaces are declared in `kernel.h`.

Supported shell commands:

- `echo <text>` prints its arguments.
- `hostname` prints `minios`.
- `time` displays the current UTC date and time from the real-time clock.
- `ls` lists files in the current root directory.
- `mkdir <name>` creates a directory in the current root directory. Names may
  contain 1-8 letters, digits, underscores, or hyphens.
- `clear` clears the screen.
- `draw` draws a red circle on a white background at 640x480 resolution.
  Press Escape to return to the bitmap-character shell.
- `basic` opens a line-numbered BASIC interpreter. Enter numbered lines to
  write a program, then use `RUN`, `LIST`, `NEW`, or `EXIT`. Programs support
  `PRINT`, `LET`, `IF ... THEN`, `GOTO`, `REM`, `END`, and integer expressions.
- `help` lists the available commands.

The build writes its intermediate files and disk image to `build\`. MSVC
links the kernel at a fixed 32-bit address; keep the loaded code small and
freestanding, without Windows APIs or C runtime dependencies.
Rebuilding recreates the disk image and resets runtime-created directories.
