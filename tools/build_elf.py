"""Compile and link a freestanding MinOS ELF32 program with Clang and LLD."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_tool(name):
    directory = os.environ.get("LLVM_BIN")
    if directory:
        tool = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
        if not tool.is_file():
            raise RuntimeError(f"{tool} was not found (LLVM_BIN).")
        return str(tool)
    found = shutil.which(name)
    if found:
        return found
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        installations = Path(local_app_data, "MinOS").glob(
            str(Path("llvm-*") / "files" / "LLVM" / "bin")
        )
        for path in sorted(installations, reverse=True):
            tool = path / (name + (".exe" if os.name == "nt" else ""))
            if tool.is_file():
                return str(tool)
    raise RuntimeError(f"{name} was not found; set LLVM_BIN or add LLVM to PATH.")


def build(sources, output):
    clang = find_tool("clang")
    linker = find_tool("ld.lld")
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = [
        "--target=i686-unknown-none-elf", "-march=i686",
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables", "-mno-sse", "-mno-mmx",
        "-msoft-float", "-O2",
        "-I", str(ROOT / "src" / "programs" / "include"),
    ]
    with tempfile.TemporaryDirectory(
        prefix="minos-elf-", dir=output.parent
    ) as directory:
        objects = []
        input_files = [
            ROOT / "src" / "elf" / "start.s",
            ROOT / "src" / "elf" / "runtime.c",
            *(Path(source).resolve() for source in sources),
        ]
        for index, input_file in enumerate(input_files):
            obj = Path(directory) / f"{index}.o"
            compile_flags = flags if input_file.suffix == ".c" else [
                "--target=i686-unknown-none-elf", "-march=i686",
            ]
            subprocess.run(
                [clang, *compile_flags, "-c", str(input_file), "-o", str(obj)],
                check=True,
            )
            objects.append(str(obj))
        temporary_output = Path(directory) / "program.elf"
        subprocess.run(
            [linker, "-m", "elf_i386", "--build-id=none", "--strip-all",
             "-T", str(ROOT / "src" / "elf" / "minos.ld"),
             "-o", str(temporary_output), *objects],
            check=True,
        )
        if temporary_output.stat().st_size > 64 * 1024:
            raise RuntimeError("ELF file exceeds the 64 KiB MinOS loader limit.")
        temporary_output.replace(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="+", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        build(args.sources, args.output)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ELF build failed: {error}", file=sys.stderr)
        return 1
    print(f"Built {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
