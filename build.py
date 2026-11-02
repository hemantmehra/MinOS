import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_DIRECTORY = ROOT / "build"


def find_nasm():
    nasm = shutil.which("nasm.exe") or shutil.which("nasm")
    if nasm:
        return Path(nasm)

    local_app_data = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    program_files_x86 = Path(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    )
    candidates = (
        local_app_data / "bin" / "NASM" / "nasm.exe",
        program_files / "NASM" / "nasm.exe",
        program_files_x86 / "NASM" / "nasm.exe",
        local_app_data / "Microsoft" / "WinGet" / "Links" / "nasm.exe",
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def find_visual_studio():
    program_files_x86 = Path(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    )
    vswhere = (
        program_files_x86
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.is_file():
        raise RuntimeError(f"Visual Studio Installer was not found at {vswhere}.")

    result = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError("Failed to locate Visual Studio using vswhere.")
    installation = result.stdout.strip()
    if not installation:
        raise RuntimeError(
            "A Visual Studio installation with the x86 MSVC C++ build tools "
            "is required."
        )
    return Path(installation) / "Common7" / "Tools" / "VsDevCmd.bat"


def run_msvc(developer_command):
    sources = (
        "kernel.c",
        "video.c",
        "filesystem.c",
        "keyboard.c",
        "interrupts.c",
        "memory.c",
        "process.c",
        "time.c",
        "shell_utils.c",
        "basic.c",
    )
    source_paths = " ".join(f'"{ROOT / source}"' for source in sources)
    object_files = " ".join(f"{Path(source).stem}.obj" for source in sources)
    compile_command = (
        f'call "{developer_command}" -arch=x86 -host_arch=x64 >nul && '
        f"cl /nologo /c /TC /GS- /Oi /O1 /Zl /arch:IA32 {source_paths} "
        "&& link /nologo /nodefaultlib /machine:x86 /subsystem:native "
        "/entry:kernel_main /base:0x10000 /fixed /filealign:512 "
        "/merge:.rdata=.text /merge:.data=.text /section:.text,ERW "
        f"/incremental:no /out:kernel.exe {object_files}"
    )
    result = subprocess.run(
        compile_command, cwd=BUILD_DIRECTORY, shell=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError("MSVC failed to compile or link the kernel.")


def main():
    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)

    nasm = find_nasm()
    if nasm is None:
        raise RuntimeError("NASM was not found. Install NASM or add it to PATH.")

    developer_command = find_visual_studio()
    run_msvc(developer_command)

    boot_sector = BUILD_DIRECTORY / "boot.bin"
    result = subprocess.run(
        [str(nasm), "-f", "bin", "-o", str(boot_sector), str(ROOT / "boot.asm")],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("NASM failed to assemble the boot sector.")

    executable = BUILD_DIRECTORY / "HELLO.EXE"
    result = subprocess.run(
        [
            str(nasm),
            "-f",
            "bin",
            "-o",
            str(executable),
            str(ROOT / "programs" / "hello.asm"),
        ],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("NASM failed to assemble HELLO.EXE.")

    python = sys.executable
    result = subprocess.run(
        [
            python,
            str(ROOT / "tools" / "make_disk.py"),
            "--boot",
            str(boot_sector),
            "--kernel",
            str(BUILD_DIRECTORY / "kernel.exe"),
            "--output",
            str(BUILD_DIRECTORY / "helloos.img"),
            "--files-root",
            str(ROOT),
            "--extra-file",
            "HELLO.EXE",
            str(executable),
        ],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("Failed to build the bootable disk image.")

    print(f"Built {BUILD_DIRECTORY / 'helloos.img'}")


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
