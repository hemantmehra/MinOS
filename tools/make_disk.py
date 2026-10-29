import argparse
import struct
from pathlib import Path


DISK_IMAGE_SIZE = 16 * 1024 * 1024
KERNEL_BASE = 0x10000
SECTOR_SIZE = 512
KERNEL_SECTOR_COUNT = 32
ENTRY_PLACEHOLDER = 0xC0DEF00D


def read_pe_kernel(path):
    image = path.read_bytes()
    if image[:2] != b"MZ":
        raise ValueError(f"{path} is not a PE executable (missing MZ signature).")

    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path} has an invalid PE signature.")

    file_header = pe_offset + 4
    machine, section_count = struct.unpack_from("<HH", image, file_header)
    optional_size = struct.unpack_from("<H", image, file_header + 16)[0]
    if machine != 0x14C:
        raise ValueError("The kernel PE must target 32-bit x86.")

    optional_header = file_header + 20
    if struct.unpack_from("<H", image, optional_header)[0] != 0x10B:
        raise ValueError("The kernel PE must use the 32-bit PE32 format.")
    entry_rva = struct.unpack_from("<I", image, optional_header + 16)[0]
    image_base = struct.unpack_from("<I", image, optional_header + 28)[0]
    if image_base != KERNEL_BASE:
        raise ValueError(
            f"Expected PE image base 0x{KERNEL_BASE:X}, got 0x{image_base:X}."
        )

    section_table = optional_header + optional_size
    text_section = None
    unexpected_sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        name = image[offset : offset + 8].split(b"\0", 1)[0].decode("ascii")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", image, offset + 8
        )
        if name == ".text":
            text_section = (virtual_size, virtual_address, raw_size, raw_offset)
        elif virtual_size or raw_size:
            unexpected_sections.append(name)

    if text_section is None:
        raise ValueError("The kernel PE does not contain a .text section.")
    if unexpected_sections:
        sections = ", ".join(unexpected_sections)
        raise ValueError(
            f"Unexpected kernel sections ({sections}); merge kernel data into .text."
        )

    virtual_size, virtual_address, raw_size, raw_offset = text_section
    if not virtual_size or not raw_size:
        raise ValueError("The kernel .text section is empty.")
    if raw_offset + raw_size > len(image):
        raise ValueError("The kernel .text section extends past the PE file.")
    if not virtual_address <= entry_rva < virtual_address + virtual_size:
        raise ValueError("The kernel PE entry point is not inside the .text section.")

    load_end = virtual_address + max(virtual_size, raw_size)
    payload_size = (load_end + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)
    sector_limit = KERNEL_SECTOR_COUNT * SECTOR_SIZE
    if payload_size > sector_limit:
        raise ValueError(
            f"Kernel payload needs {payload_size} bytes, exceeding the "
            f"{sector_limit}-byte bootloader limit."
        )

    payload = bytearray(payload_size)
    payload[virtual_address : virtual_address + raw_size] = image[
        raw_offset : raw_offset + raw_size
    ]
    return payload, KERNEL_BASE + entry_rva


def build_disk(boot_path, kernel_path, output_path):
    boot = bytearray(boot_path.read_bytes())
    if len(boot) != SECTOR_SIZE:
        raise ValueError(f"The boot sector must be exactly {SECTOR_SIZE} bytes.")
    if boot[510:512] != b"\x55\xaa":
        raise ValueError("The boot sector is missing its 0x55AA signature.")

    placeholder = struct.pack("<I", ENTRY_PLACEHOLDER)
    if boot.count(placeholder) != 1:
        raise ValueError("Expected exactly one kernel entry-point placeholder.")

    payload, entry_point = read_pe_kernel(kernel_path)
    entry_offset = boot.index(placeholder)
    boot[entry_offset : entry_offset + 4] = struct.pack("<I", entry_point)

    disk = bytearray(DISK_IMAGE_SIZE)
    disk[:SECTOR_SIZE] = boot
    start = SECTOR_SIZE
    disk[start : start + len(payload)] = payload
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(disk)
    print(
        f"Created {output_path} ({len(disk)} bytes); "
        f"kernel entry point 0x{entry_point:X}."
    )


def main():
    parser = argparse.ArgumentParser(description="Package the MSVC kernel for QEMU.")
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    build_disk(arguments.boot, arguments.kernel, arguments.output)


if __name__ == "__main__":
    main()
