# MinOS

A tiny 32-bit x86 kernel that writes `Hello, world!` to VGA text memory and
halts. MSVC builds the freestanding C kernel, NASM assembles a BIOS boot
sector, and QEMU runs the resulting disk image.

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

QEMU opens the kernel with a BIOS-backed PC. The C kernel writes the greeting
to the VGA text screen and then halts.

The build writes its intermediate files and disk image to `build\`. MSVC
links the kernel at a fixed 32-bit address; keep the loaded code small and
freestanding, without Windows APIs or C runtime dependencies.
