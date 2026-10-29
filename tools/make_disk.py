import argparse
import math
import struct
from pathlib import Path


DISK_IMAGE_SIZE = 16 * 1024 * 1024
KERNEL_BASE = 0x10000
SECTOR_SIZE = 512
KERNEL_SECTOR_COUNT = 32
ENTRY_PLACEHOLDER = 0xC0DEF00D
FILESYSTEM_START_SECTOR = KERNEL_SECTOR_COUNT + 1
FAT16_ROOT_ENTRY_COUNT = 512
FAT16_RESERVED_SECTORS = 1
FAT16_COUNT = 2
FAT16_MEDIA_DESCRIPTOR = 0xF8
FAT16_END_OF_CHAIN = 0xFFFF


class Directory:
    def __init__(self, source, short_name):
        self.source = source
        self.short_name = short_name
        self.children = []
        self.first_cluster = 0


class File:
    def __init__(self, source, short_name, contents):
        self.source = source
        self.short_name = short_name
        self.contents = contents
        self.first_cluster = 0


def create_short_name(name, used_names):
    if "." in name and not name.startswith("."):
        base, extension = name.rsplit(".", 1)
    else:
        base, extension = name, ""

    def sanitize(value):
        characters = []
        for character in value.upper():
            if "A" <= character <= "Z" or "0" <= character <= "9":
                characters.append(character)
            elif character in "$%'-_@~`!(){}^#&":
                characters.append(character)
            else:
                characters.append("_")
        return "".join(characters) or "_"

    sanitized_base = sanitize(base)
    sanitized_extension = sanitize(extension) if extension else ""
    candidate_base = sanitized_base[:8]
    candidate_extension = sanitized_extension[:3]
    candidate = (
        candidate_base.ljust(8) + candidate_extension.ljust(3)
    ).encode("ascii")
    if candidate not in used_names:
        used_names.add(candidate)
        return candidate

    for suffix in range(1, 1000000):
        marker = f"~{suffix}"
        prefix = sanitized_base[: 8 - len(marker)]
        candidate = (
            (prefix + marker).ljust(8) + sanitized_extension[:3].ljust(3)
        ).encode("ascii")
        if candidate not in used_names:
            used_names.add(candidate)
            return candidate

    raise ValueError(f"Unable to create a unique FAT16 name for {name!r}.")


def source_entries(directory):
    excluded_directories = {".git", "build", "__pycache__"}
    entries = []
    for child in sorted(directory.iterdir(), key=lambda item: (item.name.casefold(), item.name)):
        if child.is_symlink():
            continue
        if child.is_dir():
            if child.name.casefold() not in excluded_directories:
                entries.append((child, None))
        elif child.is_file() and child.suffix.casefold() != ".pyc":
            entries.append((child, child.read_bytes()))
    return entries


def build_directory(source, short_name):
    directory = Directory(source, short_name)
    used_names = set()

    for child, contents in source_entries(source):
        name = create_short_name(child.name, used_names)
        if contents is None:
            directory.children.append(build_directory(child, name))
        else:
            directory.children.append(File(child, name, contents))

    return directory


def write_fat_entry(fat, cluster, value):
    struct.pack_into("<H", fat, cluster * 2, value)


def allocate_clusters(count, next_cluster, max_cluster, fat):
    end = next_cluster + count
    if end > max_cluster + 1:
        raise ValueError(
            "The files in the workspace exceed the available FAT16 disk space."
        )
    clusters = list(range(next_cluster, end))
    for index, cluster in enumerate(clusters):
        next_value = clusters[index + 1] if index + 1 < count else FAT16_END_OF_CHAIN
        write_fat_entry(fat, cluster, next_value)
    return clusters, end


def directory_entry(short_name, attributes, first_cluster=0, file_size=0):
    entry = bytearray(32)
    entry[:11] = short_name
    entry[11] = attributes
    entry[16] = 0x21
    entry[24] = 0x21
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, file_size)
    return entry


def encode_directory(directory, parent_cluster):
    entries = []
    if directory.source is not None:
        entries.append(directory_entry(b".          ", 0x10, directory.first_cluster))
        entries.append(directory_entry(b"..         ", 0x10, parent_cluster))

    for child in directory.children:
        if isinstance(child, Directory):
            entries.append(directory_entry(child.short_name, 0x10, child.first_cluster))
        else:
            entries.append(
                directory_entry(
                    child.short_name,
                    0x20,
                    child.first_cluster,
                    len(child.contents),
                )
            )
    entries.append(bytearray(32))
    return b"".join(entries)


def populate_fat16(volume, files_root):
    total_sectors = len(volume) // SECTOR_SIZE
    root_sectors = math.ceil(FAT16_ROOT_ENTRY_COUNT * 32 / SECTOR_SIZE)
    fat_sectors = 1
    while True:
        data_clusters = (
            total_sectors
            - FAT16_RESERVED_SECTORS
            - FAT16_COUNT * fat_sectors
            - root_sectors
        )
        required_fat_sectors = math.ceil((data_clusters + 2) * 2 / SECTOR_SIZE)
        if required_fat_sectors == fat_sectors:
            break
        fat_sectors = required_fat_sectors

    if not 4085 <= data_clusters < 65525:
        raise ValueError("The boot disk image cannot be represented as FAT16.")

    root_sector = FAT16_RESERVED_SECTORS + FAT16_COUNT * fat_sectors
    data_sector = root_sector + root_sectors
    fat = bytearray(fat_sectors * SECTOR_SIZE)
    write_fat_entry(fat, 0, (FAT16_MEDIA_DESCRIPTOR << 8) | 0xFF)
    write_fat_entry(fat, 1, FAT16_END_OF_CHAIN)
    root = Directory(None, b"MINIOS     ")
    next_cluster = 2
    children = build_directory(files_root, root.short_name).children

    def allocate_directory(directory, parent_cluster):
        nonlocal next_cluster
        encoded = encode_directory(directory, parent_cluster)
        cluster_count = max(1, math.ceil(len(encoded) / SECTOR_SIZE))
        clusters, next_cluster = allocate_clusters(
            cluster_count, next_cluster, data_clusters + 1, fat
        )
        directory.first_cluster = clusters[0]
        encoded = encode_directory(directory, parent_cluster)
        for cluster in clusters:
            sector = data_sector + cluster - 2
            start = sector * SECTOR_SIZE
            chunk_index = cluster - clusters[0]
            chunk = encoded[
                chunk_index * SECTOR_SIZE : (chunk_index + 1) * SECTOR_SIZE
            ]
            volume[start : start + len(chunk)] = chunk
        for child in directory.children:
            if isinstance(child, Directory):
                allocate_directory(child, directory.first_cluster)

    def allocate_file(file):
        nonlocal next_cluster
        if not file.contents:
            return
        cluster_count = math.ceil(len(file.contents) / SECTOR_SIZE)
        clusters, next_cluster = allocate_clusters(
            cluster_count, next_cluster, data_clusters + 1, fat
        )
        file.first_cluster = clusters[0]
        for index, cluster in enumerate(clusters):
            sector = data_sector + cluster - 2
            start = sector * SECTOR_SIZE
            chunk = file.contents[index * SECTOR_SIZE : (index + 1) * SECTOR_SIZE]
            volume[start : start + len(chunk)] = chunk

    root.children = children
    if len(root.children) + 2 > FAT16_ROOT_ENTRY_COUNT:
        raise ValueError("The workspace has too many root entries for this FAT16 disk.")
    for child in root.children:
        if isinstance(child, Directory):
            allocate_directory(child, 0)
        else:
            allocate_file(child)

    root_data = bytearray(root_sectors * SECTOR_SIZE)
    root_entry = directory_entry(root.short_name, 0x08)
    root_data[:32] = root_entry
    for index, child in enumerate(root.children, start=1):
        if isinstance(child, Directory):
            entry = directory_entry(child.short_name, 0x10, child.first_cluster)
        else:
            entry = directory_entry(
                child.short_name, 0x20, child.first_cluster, len(child.contents)
            )
        offset = index * 32
        root_data[offset : offset + 32] = entry

    volume[root_sector * SECTOR_SIZE : data_sector * SECTOR_SIZE] = root_data
    for copy_index in range(FAT16_COUNT):
        start = (FAT16_RESERVED_SECTORS + copy_index * fat_sectors) * SECTOR_SIZE
        volume[start : start + len(fat)] = fat

    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[:3] = b"\xeb\x3c\x90"
    boot_sector[3:11] = b"MINIOS  "
    struct.pack_into("<H", boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = 1
    struct.pack_into("<H", boot_sector, 14, FAT16_RESERVED_SECTORS)
    boot_sector[16] = FAT16_COUNT
    struct.pack_into("<H", boot_sector, 17, FAT16_ROOT_ENTRY_COUNT)
    struct.pack_into("<H", boot_sector, 19, total_sectors)
    boot_sector[21] = FAT16_MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot_sector, 22, fat_sectors)
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 16)
    struct.pack_into("<I", boot_sector, 28, FILESYSTEM_START_SECTOR)
    boot_sector[36] = 0x80
    boot_sector[38] = 0x29
    struct.pack_into("<I", boot_sector, 39, 0x4D696E4F)
    boot_sector[43:54] = b"MINIOS     "
    boot_sector[54:62] = b"FAT16   "
    boot_sector[510:512] = b"\x55\xaa"
    volume[:SECTOR_SIZE] = boot_sector

    print(
        f"Created FAT16 root with {len(root.children)} entries, "
        f"{data_clusters} clusters, and {fat_sectors} sectors per FAT."
    )


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


def build_disk(boot_path, kernel_path, output_path, files_root):
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
    filesystem_start = FILESYSTEM_START_SECTOR * SECTOR_SIZE
    populate_fat16(
        memoryview(disk)[filesystem_start:],
        files_root,
    )
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
    parser.add_argument("--files-root", type=Path, required=True)
    arguments = parser.parse_args()
    build_disk(
        arguments.boot,
        arguments.kernel,
        arguments.output,
        arguments.files_root,
    )


if __name__ == "__main__":
    main()
