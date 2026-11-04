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
characters and a blue `minios>` prompt. The system disk contains an ext2
filesystem populated with project files and directories. A separate 8 MiB
ext2 data disk is attached as the secondary IDE drive at
`build\disks\data.img`; its contents persist across normal builds and are
available as drive `B:`. The system disk image is `build\disks\system.img` and
is drive `A:`.

All operating-system source code is under `src\`. Kernel code is organized by
subsystem: `src\kernel.c` handles shell dispatch, `src\video.c` renders the
VGA terminal, `src\keyboard.c` reads keyboard input, `src\filesystem.c`
implements ext2 access, and `src\filesystem_directory.c` handles ext2
directory creation. The filesystem APIs are declared in `src\filesystem.h`;
`src\interrupts.c` sets up the IDT and keyboard IRQ,
`src\time.c` reads the
CMOS real-time clock, `src\memory.c` initializes x86 paging and manages 4 KiB
physical pages, and `src\shell_utils.c` provides shared shell helpers. The
boot sector is `src\boot.asm`; bundled program sources are in
`src\programs\`. Paging identity-maps the first 16 MiB and provides APIs to
allocate/free physical pages and map, unmap, and translate virtual addresses.
CPU exceptions halt safely, keyboard input is delivered through IRQ1, and the
PIT timer drives the round-robin scheduler in `src\process.c`. The shell and
each loaded process run as one schedulable thread. The scheduler saves and
restores complete general-register, segment-register, and interrupt contexts,
and programs use `int 0x80` to print or exit. Kernel and sample programs are
built for IA-32 integer code without SIMD or floating-point state.

Bundled applications use the procedural MinOS High language. Each
`src\programs\*.high` source is compiled by `tools\high_compiler.py` into NASM
assembly under `build\programs\`, then assembled into an executable for the
ext2 image. See [docs/high-language.md](docs/high-language.md) for syntax and
built-in operations.

Processes currently run in ring 0 and share the kernel address space; they
are preemptively scheduled but are not privilege-isolated. Root-directory
executables use the MinOS `MEXE` format: a 12-byte header (`MEXE`, 32-bit
image size, and 32-bit entry offset) followed by flat 32-bit x86 code. The
kernel loads images up to 64 KiB from the ext2 root directory. The ext2
filesystem uses 1 KiB blocks and is stored directly after the boot sector and
kernel, without an MBR partition table. Kernel services expose directory
streams and directory creation to the High-language programs.
`src\programs\hello.high` is compiled to `HELLO.EXE` during the build and
included in the image. Core interfaces are declared in `src\kernel.h`, process
interfaces in `src\process.h`, and filesystem interfaces in
`src\filesystem.h`. The print call uses `EAX=1` and a null-terminated string
pointer in `EBX`; `EAX=0` exits
the process. The `hostname`, `time`, `mem`, `ps`, `ls`, `pwd`, `cd`, `cat`, `mkdir`,
`kill`, `clear`, `draw`, and `basic` commands are High-language programs
which use kernel services for OS operations; `echo` remains a built-in shell
command.
The shell supports a single pipeline between programs, such as `ls | wc`.
`src\programs\wc.high` compiles to `WC.EXE` and counts whitespace-separated
words read from pipeline input.
`src\programs\countdown.high` is compiled as `COUNTDOWN.EXE`; execute it to print
the numbers 10 through 1.
`src\programs\paint.high` is compiled as `PAINT.EXE`; execute `PAINT.EXE` to draw
a red circle, green triangle, and blue rectangle. Press Escape to exit and
restore the shell display. Applications can poll keyboard scancodes with
`EAX=2` (returns a scancode in `EAX`, or zero when no key is available).
`src\programs\factorial.high` is compiled as `FACTORIAL.EXE`; run it and enter
a number from 0 to 12 to display its factorial.
`src\programs\stack.high` is compiled as `STACK.EXE`; it provides `push <n>`,
`pop`, `top`, and `exit` commands using a 100-element integer array.
`src\programs\ball.high` is compiled as `BALL.EXE`; it animates a red ball
that bounces off the screen edges. Each launch chooses a random starting
position and direction; press Escape to exit.

Supported shell commands:

- `echo <text>` prints its arguments.
- `hostname` runs `HOSTNAME.EXE` and prints `minios`.
- `time` runs `TIME.EXE` to display the current UTC date and time.
- `mem` runs `MEM.EXE` to report free 4 KiB pages and test virtual mapping.
- `ps` runs `PS.EXE` to list process/thread IDs and states.
- Enter an executable name such as `HELLO.EXE` (or `HELLO`) to start it.
  The included program prints a hello message and exits.
- Enter `COUNTDOWN.EXE` to print 10 through 1.
- Enter `FACTORIAL.EXE` and provide a number from 0 to 12 to calculate its
  factorial.
- Enter `STACK.EXE` to use a 100-item stack with `push <n>`, `pop`, `top`,
  and `exit` commands.
- Enter `BALL.EXE` to watch a red ball bounce from a randomized position and
  direction. Press Escape to return to the shell.
- Add `&` to run a process in the background without blocking the shell, for
  example `ball &`. Use `kill <process-name>` to terminate a background
  process, such as `kill ball`.
- Enter `PAINT.EXE` to run the included drawing program. Press Escape to
  return to the shell.
- `ls [path]` runs `LS.EXE` to list the current directory or the requested
  absolute or relative directory path. Entries beginning with `.` are hidden.
- `pwd` runs `PWD.EXE` to print the current directory.
- `cd <path>` runs `CD.EXE` to change the current directory. Paths may be
  absolute or relative and support `.` and `..`.
- `cat <file>` runs `CAT.EXE` to display a file using file stream services.
- `A:/<path>` addresses the system disk and `B:/<path>` addresses the secondary
  ext2 disk. For example, `A:/ball.exe` runs the system-disk executable,
  `cat B:/SAMPLE.TXT` reads a data-disk file, and
  `write B:/WRITE.TXT <text>` replaces the writable file's contents.
  Writes are limited to the file's preallocated ext2 capacity.
- `python tools\read_disk.py` lists the root of `build\disks\data.img`, or
  `python tools\read_disk.py build\disks\data.img /SAMPLE.TXT` reads a file after
  MinOS is shut down. The script can inspect any supported ext2 image.
- `wc` runs `WC.EXE` to count whitespace-separated words from pipeline input.
- `program [args] | program [args]` pipes one program's printed output to the
  next program's standard input. For example, `ls | wc` counts visible
  directory entries. Only one pipe is supported, and its in-memory buffer is
  limited to 16 KiB.
- `mkdir <path>` runs `MKDIR.EXE` to create a directory in the current or
  specified path, including `B:/<name>` on the secondary disk. The final
  name must contain 1-8 letters, digits, underscores, or hyphens.
- `clear` runs `CLEAR.EXE` to clear the screen.
- `draw` runs `DRAW.EXE`, which draws a red circle on white in High and uses
  primitive graphics services. It draws into a back buffer and presents the
  completed frame; press Escape to return to the shell.
- `basic` runs `BASIC.EXE`, which opens a line-numbered BASIC interpreter.
  The interpreter is implemented in High; it supports numbered lines,
  `RUN`, `LIST`, `NEW`, `EXIT`, `PRINT`, `LET`, `IF ... THEN`, `GOTO`, `REM`,
  `END`, `STOP`, and integer expressions.

The build keeps boot, kernel, program, disk, and diagnostic outputs in
`build\boot`, `build\kernel`, `build\programs`, `build\disks`, and
`build\diagnostics`. Its cleanup removes only generated build products,
preserves the data disk, and moves legacy diagnostic captures into the
diagnostics folder.
`python tools\make_data_disk.py` explicitly recreates the secondary ext2 disk
from `tools\data_files\`; this resets its contents. MSVC
links the kernel at a fixed 32-bit address; keep the loaded code small and
freestanding, without Windows APIs or C runtime dependencies.
Rebuilding recreates the system disk and resets runtime-created directories,
but preserves an existing secondary data disk.
