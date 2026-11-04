import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_DIRECTORY = ROOT / "build"
BOOT_DIRECTORY = BUILD_DIRECTORY / "boot"
KERNEL_DIRECTORY = BUILD_DIRECTORY / "kernel"
PROGRAMS_DIRECTORY = BUILD_DIRECTORY / "programs"
DISKS_DIRECTORY = BUILD_DIRECTORY / "disks"
DIAGNOSTICS_DIRECTORY = BUILD_DIRECTORY / "diagnostics"
SYSTEM_IMAGE = DISKS_DIRECTORY / "system.img"
DATA_IMAGE = DISKS_DIRECTORY / "data.img"


def _move_legacy_file(source, destination):
    if not source.exists():
        return
    if destination.exists():
        raise RuntimeError(
            f"Both legacy and organized build files exist: {source} and "
            f"{destination}. Move or compare them before rebuilding."
        )
    shutil.move(str(source), str(destination))


def prepare_build_directories():
    for directory in (
        BOOT_DIRECTORY,
        KERNEL_DIRECTORY,
        PROGRAMS_DIRECTORY,
        DISKS_DIRECTORY,
        DIAGNOSTICS_DIRECTORY,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    _move_legacy_file(BUILD_DIRECTORY / "data.img", DATA_IMAGE)

    for name in ("boot.bin", "kernel.exe", "helloos.img"):
        legacy_file = BUILD_DIRECTORY / name
        if legacy_file.exists():
            legacy_file.unlink()

    for directory, suffixes in (
        (BOOT_DIRECTORY, {".bin"}),
        (KERNEL_DIRECTORY, {".obj", ".cod", ".exe", ".ilk", ".pdb"}),
        (PROGRAMS_DIRECTORY, {".asm", ".exe"}),
    ):
        for path in directory.iterdir():
            if path.is_file() and path.suffix.lower() in suffixes:
                path.unlink()

    if SYSTEM_IMAGE.exists():
        SYSTEM_IMAGE.unlink()

    legacy_diagnostics = (
        path
        for path in BUILD_DIRECTORY.iterdir()
        if path.is_file()
        and (
            path.suffix.lower() in {".log", ".ppm", ".png"}
            or path.stem.lower().endswith("-check")
        )
    )
    for path in legacy_diagnostics:
        _move_legacy_file(path, DIAGNOSTICS_DIRECTORY / path.name)

    for path in BUILD_DIRECTORY.iterdir():
        if path.is_file() and (
            path.suffix.lower() in {".obj", ".exe", ".cod"}
            or path.name.lower() in {"boot.bin", "helloos.img"}
        ):
            path.unlink()


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
        "filesystem_directory.c",
        "keyboard.c",
        "interrupts.c",
        "memory.c",
        "process.c",
        "time.c",
        "shell_utils.c",
    )
    source_paths = " ".join(f'"{ROOT / "src" / source}"' for source in sources)
    object_files = " ".join(
        f"{Path(source).stem}.obj" for source in sources
    )
    compile_command = (
        f'call "{developer_command}" -arch=x86 -host_arch=x64 >nul && '
        f"cl /nologo /c /TC /GS- /Oi /O1 /Zl /arch:IA32 {source_paths} "
        "&& link /nologo /nodefaultlib /machine:x86 /subsystem:native "
        "/entry:kernel_main /base:0x10000 /fixed /filealign:512 "
        "/merge:.rdata=.text /merge:.data=.text /section:.text,ERW "
        f"/incremental:no /out:kernel.exe {object_files}"
    )
    result = subprocess.run(
        compile_command, cwd=KERNEL_DIRECTORY, shell=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError("MSVC failed to compile or link the kernel.")


def main():
    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)

    nasm = find_nasm()
    if nasm is None:
        raise RuntimeError("NASM was not found. Install NASM or add it to PATH.")

    developer_command = find_visual_studio()
    prepare_build_directories()
    run_msvc(developer_command)

    boot_sector = BOOT_DIRECTORY / "boot.bin"
    result = subprocess.run(
        [
            str(nasm),
            "-f",
            "bin",
            "-o",
            str(boot_sector),
            str(ROOT / "src" / "boot.asm"),
        ],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("NASM failed to assemble the boot sector.")

    compiler = ROOT / "tools" / "high_compiler.py"
    program_sources = sorted((ROOT / "src" / "programs").glob("*.high"))
    if not program_sources:
        raise RuntimeError("No .high program sources were found.")

    executables = []
    for source in program_sources:
        assembly = PROGRAMS_DIRECTORY / f"{source.stem}.asm"
        executable = PROGRAMS_DIRECTORY / f"{source.stem.upper()}.EXE"
        result = subprocess.run(
            [sys.executable, str(compiler), str(source), str(assembly)],
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(f"Failed to compile {source.name}.")

        result = subprocess.run(
            [
                str(nasm),
                "-f",
                "bin",
                "-o",
                str(executable),
                str(assembly),
            ],
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(f"NASM failed to assemble {executable.name}.")
        executables.append(executable)

    python = sys.executable
    disk_command = [
        python,
        str(ROOT / "tools" / "make_disk.py"),
        "--boot",
        str(boot_sector),
        "--kernel",
        str(KERNEL_DIRECTORY / "kernel.exe"),
        "--output",
        str(SYSTEM_IMAGE),
        "--files-root",
        str(ROOT),
    ]
    for executable in executables:
        disk_command.extend(
            ["--extra-file", executable.name, str(executable)]
        )
    result = subprocess.run(disk_command, check=False)
    if result.returncode != 0:
        raise RuntimeError("Failed to build the bootable disk image.")

    if not DATA_IMAGE.exists():
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "make_data_disk.py"),
                "--output",
                str(DATA_IMAGE),
            ],
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError("Failed to create the ext2 secondary disk.")

    print(f"Built {SYSTEM_IMAGE}")


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
