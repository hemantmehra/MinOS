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

From PowerShell in this directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

The execution-policy bypass applies only to each PowerShell process; it does
not change the system or user execution policy.

QEMU opens the kernel with a BIOS-backed PC. The kernel clears the VGA text
screen, prints the greeting, and displays a keyboard-driven `minios>` prompt.
The disk image contains a writable FAT16 filesystem populated with project
files and directories.

Supported shell commands:

- `echo <text>` prints its arguments.
- `hostname` prints `minios`.
- `ls` lists files in the current root directory.
- `mkdir <name>` creates a directory in the current root directory. Names may
  contain 1-8 letters, digits, underscores, or hyphens.
- `clear` clears the screen.
- `help` lists the available commands.

The build writes its intermediate files and disk image to `build\`. MSVC
links the kernel at a fixed 32-bit address; keep the loaded code small and
freestanding, without Windows APIs or C runtime dependencies.
Rebuilding recreates the disk image and resets runtime-created directories.
