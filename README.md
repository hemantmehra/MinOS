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

QEMU opens the kernel with a BIOS-backed PC. The kernel uses a 1280x720 VBE
framebuffer with a 16-color palette, rendering the shell with bitmap
characters and a blue `minios>` prompt. The disk image contains a read-only
ext2 filesystem populated with project files and directories.

Kernel code is organized by subsystem: `kernel.c` handles shell dispatch,
`video.c` renders the VGA terminal, `keyboard.c` reads keyboard input,
`filesystem.c` implements read-only ext2 access, `basic.c` contains the BASIC
interpreter, `interrupts.c` sets up the IDT and keyboard IRQ, `time.c` reads
the CMOS real-time clock, `memory.c` initializes x86 paging and manages 4 KiB
physical pages, and `shell_utils.c` provides shared shell helpers. Paging
identity-maps the first 16 MiB and provides APIs to allocate/free physical
pages and map, unmap, and translate virtual addresses. CPU exceptions halt
safely, keyboard input is delivered through IRQ1, and the PIT timer drives
the round-robin scheduler in `process.c`. The shell and each loaded process
run as one schedulable thread. The scheduler saves and restores complete
general-register, segment-register, and interrupt contexts, and programs use
`int 0x80` to print or exit. Kernel and sample programs are built for IA-32
integer code without SIMD or floating-point state.

Processes currently run in ring 0 and share the kernel address space; they
are preemptively scheduled but are not privilege-isolated. Root-directory
executables use the MinOS `MEXE` format: a 12-byte header (`MEXE`, 32-bit
image size, and 32-bit entry offset) followed by flat 32-bit x86 code. The
kernel loads images up to 64 KiB from the ext2 root directory. The ext2
filesystem uses 1 KiB blocks and is stored directly after the boot sector and
kernel, without an MBR partition table. The kernel supports read-only root
directory listing and executable loading; `mkdir` reports that the filesystem
is read-only. `programs/hello.asm` is assembled as `HELLO.EXE` during the
build and included in the image. Shared
interfaces are declared in `kernel.h`. The print call uses `EAX=1` and a
null-terminated string pointer in `EBX`; `EAX=0` exits the process.
`programs/countdown.asm` is assembled as `COUNTDOWN.EXE`; execute it to print
the numbers 10 through 1.
`programs/paint.asm` is assembled as `PAINT.EXE`; execute `PAINT.EXE` to draw
a red circle, green triangle, and blue rectangle. Press Escape to exit and
restore the shell display. Applications can poll keyboard scancodes with
`EAX=2` (returns a scancode in `EAX`, or zero when no key is available).

Supported shell commands:

- `echo <text>` prints its arguments.
- `hostname` prints `minios`.
- `time` displays the current UTC date and time from the real-time clock.
- `mem` reports free 4 KiB pages and tests virtual page mapping.
- `ps` lists the shell and active process/thread IDs and states.
- Enter an executable name such as `HELLO.EXE` (or `HELLO`) to start it.
  The included program prints a hello message and exits.
- Enter `COUNTDOWN.EXE` to print 10 through 1.
- Enter `PAINT.EXE` to run the included drawing program. Press Escape to
  return to the shell.
- `ls` lists files in the current root directory.
- `mkdir <name>` reports that the ext2 filesystem is read-only.
- `clear` clears the screen.
- `draw` draws a red circle on a white background at 1280x720 resolution.
  Press Escape to return to the bitmap-character shell.
- `basic` opens a line-numbered BASIC interpreter. Enter numbered lines to
  write a program, then use `RUN`, `LIST`, `NEW`, or `EXIT`. Programs support
  `PRINT`, `LET`, `IF ... THEN`, `GOTO`, `REM`, `END`, and integer expressions.
- `help` lists the available commands.

The build writes its intermediate files and disk image to `build\`. MSVC
links the kernel at a fixed 32-bit address; keep the loaded code small and
freestanding, without Windows APIs or C runtime dependencies.
Rebuilding recreates the disk image and resets runtime-created directories.
