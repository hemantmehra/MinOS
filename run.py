import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
IMAGE = ROOT / "build" / "disks" / "system.img"
DATA_IMAGE = ROOT / "build" / "disks" / "data.img"


def find_qemu():
    qemu = shutil.which("qemu-system-i386.exe") or shutil.which(
        "qemu-system-i386"
    )
    if qemu:
        return Path(qemu)

    candidates = (
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "qemu"
        / "qemu-system-i386.exe",
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def main():
    if not IMAGE.is_file():
        raise RuntimeError("Disk image not found. Run python .\\build.py first.")
    if not DATA_IMAGE.is_file():
        raise RuntimeError(
            "Secondary data disk not found. Run python .\\build.py first."
        )

    qemu = find_qemu()
    if qemu is None:
        raise RuntimeError(
            "QEMU i386 was not found. Install QEMU or add it to PATH."
        )

    result = subprocess.run(
        [
            str(qemu),
            "-machine",
            "pc",
            "-m",
            "16M",
            "-drive",
            f"file={IMAGE},format=raw,if=ide",
            "-drive",
            f"file={DATA_IMAGE},format=raw,if=ide,index=1",
            "-boot",
            "order=c",
            "-no-reboot",
        ],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"QEMU exited with code {result.returncode}.")


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
