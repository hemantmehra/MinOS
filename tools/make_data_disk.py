import argparse
from pathlib import Path

from make_disk import populate_ext2


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = Path(__file__).resolve().parent / "data_files"
DEFAULT_OUTPUT = ROOT / "build" / "disks" / "data.img"
DISK_IMAGE_SIZE = 8 * 1024 * 1024


def main():
    parser = argparse.ArgumentParser(
        description="Create an ext2 secondary disk image for MinOS."
    )
    parser.add_argument("--files-root", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()

    if not arguments.files_root.is_dir():
        raise ValueError(f"Data source directory does not exist: {arguments.files_root}")

    image = bytearray(DISK_IMAGE_SIZE)
    populate_ext2(memoryview(image), arguments.files_root)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(image)
    print(f"Created ext2 data disk {arguments.output} ({len(image)} bytes).")


if __name__ == "__main__":
    main()
