import argparse
import math
import struct
from pathlib import Path


DISK_IMAGE_SIZE = 16 * 1024 * 1024
KERNEL_BASE = 0x10000
SECTOR_SIZE = 512
KERNEL_SECTOR_COUNT = 224
ENTRY_PLACEHOLDER = 0xC0DEF00D
FILESYSTEM_START_SECTOR = KERNEL_SECTOR_COUNT + 1
EXT2_BLOCK_SIZE = 1024
EXT2_FIRST_DATA_BLOCK = 1
EXT2_BLOCKS_PER_GROUP = 8192
EXT2_INODES_PER_GROUP = 1024
EXT2_INODE_SIZE = 128
EXT2_ROOT_INODE = 2
EXT2_FIRST_DYNAMIC_INODE = 11
EXT2_FEATURE_INCOMPAT_FILETYPE = 0x0002
EXT2_S_IFREG = 0x8000
EXT2_S_IFDIR = 0x4000
EXT2_DIRECTORY_FILE_TYPE = 1
EXT2_DIRECTORY_FILE_TYPE_DIR = 2


class Directory:
    def __init__(self, source, name):
        self.source = source
        self.name = name
        self.children = []
        self.inode = 0
        self.parent_inode = 0
        self.contents = b""
        self.data_blocks = []
        self.block_pointers = [0] * 15
        self.indirect_blocks = []


class File:
    def __init__(self, source, name, contents):
        self.source = source
        self.name = name
        self.contents = contents
        self.inode = 0
        self.parent_inode = 0
        self.data_blocks = []
        self.block_pointers = [0] * 15
        self.indirect_blocks = []


def source_entries(directory):
    excluded_directories = {".git", "build", "__pycache__", "data_files"}
    entries = []
    for child in sorted(
        directory.iterdir(),
        key=lambda item: (item.name.casefold(), item.name),
    ):
        if child.is_symlink():
            continue
        if child.is_dir():
            if child.name.casefold() not in excluded_directories:
                entries.append((child, None))
        elif child.is_file() and child.suffix.casefold() != ".pyc":
            entries.append((child, child.read_bytes()))
    return entries


def build_directory(source, name):
    directory = Directory(source, name)
    if source is None:
        return directory

    for child, contents in source_entries(source):
        if contents is None:
            directory.children.append(build_directory(child, child.name))
        else:
            directory.children.append(File(child, child.name, contents))
    return directory


def validate_name(name):
    encoded = name.encode("utf-8")
    if (
        not encoded
        or len(encoded) > 255
        or b"/" in encoded
        or b"\0" in encoded
        or name in (".", "..")
    ):
        raise ValueError(f"Invalid ext2 filename: {name!r}.")


def assign_inode_children(directory, next_inode, nodes):
    for child in directory.children:
        child.parent_inode = directory.inode
        if next_inode > 2048:
            raise ValueError("The workspace has too many files for the ext2 image.")
        child.inode = next_inode
        next_inode += 1
        nodes.append(child)
        if isinstance(child, Directory):
            next_inode = assign_inode_children(child, next_inode, nodes)
    return next_inode


def encode_directory(directory):
    entries = [
        (directory.inode, ".", EXT2_DIRECTORY_FILE_TYPE_DIR),
        (directory.parent_inode, "..", EXT2_DIRECTORY_FILE_TYPE_DIR),
    ]
    for child in directory.children:
        file_type = (
            EXT2_DIRECTORY_FILE_TYPE_DIR
            if isinstance(child, Directory)
            else EXT2_DIRECTORY_FILE_TYPE
        )
        entries.append((child.inode, child.name, file_type))

    encoded = bytearray()
    entry_index = 0
    while entry_index < len(entries):
        block = bytearray(EXT2_BLOCK_SIZE)
        block_offset = 0
        while entry_index < len(entries):
            inode, name, file_type = entries[entry_index]
            name_bytes = name.encode("utf-8")
            minimum_length = (8 + len(name_bytes) + 3) & ~3
            remaining = EXT2_BLOCK_SIZE - block_offset
            if minimum_length > remaining:
                raise ValueError(f"Directory entry does not fit: {name!r}.")

            next_minimum_length = 0
            if entry_index + 1 < len(entries):
                next_name = entries[entry_index + 1][1].encode("utf-8")
                next_minimum_length = (8 + len(next_name) + 3) & ~3
            last_in_block = (
                entry_index + 1 == len(entries)
                or minimum_length + next_minimum_length > remaining
            )
            record_length = remaining if last_in_block else minimum_length

            struct.pack_into(
                "<I H B B",
                block,
                block_offset,
                inode,
                record_length,
                len(name_bytes),
                file_type,
            )
            block[block_offset + 8 : block_offset + 8 + len(name_bytes)] = (
                name_bytes
            )
            block_offset += record_length
            entry_index += 1
            if last_in_block:
                break

        encoded.extend(block)
    return bytes(encoded)


def write_block(volume, block_number, data):
    if len(data) != EXT2_BLOCK_SIZE:
        raise ValueError("An ext2 block must be exactly 1024 bytes.")
    start = block_number * EXT2_BLOCK_SIZE
    end = start + EXT2_BLOCK_SIZE
    if end > len(volume):
        raise ValueError("The ext2 filesystem image is out of space.")
    volume[start:end] = data


def pack_inode(
    node,
    directory_links,
):
    inode = bytearray(EXT2_INODE_SIZE)
    is_directory = isinstance(node, Directory)
    mode = EXT2_S_IFDIR | 0o755 if is_directory else EXT2_S_IFREG | 0o644
    size = len(node.contents)
    sectors = (len(node.data_blocks) + len(node.indirect_blocks)) * 2
    struct.pack_into("<H", inode, 0, mode)
    struct.pack_into("<I", inode, 4, size)
    struct.pack_into(
        "<H",
        inode,
        26,
        2 + directory_links if is_directory else 1,
    )
    struct.pack_into("<I", inode, 28, sectors)
    for index, block in enumerate(node.block_pointers):
        struct.pack_into("<I", inode, 40 + index * 4, block)
    return inode


def allocate_inode_blocks(node, allocate_block, write_block_callback):
    if isinstance(node, Directory):
        node.contents = encode_directory(node)
    if not node.contents:
        return

    block_count = math.ceil(len(node.contents) / EXT2_BLOCK_SIZE)
    node.data_blocks = [allocate_block() for _ in range(block_count)]
    for index, block_number in enumerate(node.data_blocks):
        chunk = node.contents[
            index * EXT2_BLOCK_SIZE : (index + 1) * EXT2_BLOCK_SIZE
        ]
        write_block_callback(
            block_number,
            chunk.ljust(EXT2_BLOCK_SIZE, b"\0"),
        )

    direct_count = min(12, block_count)
    node.block_pointers[:direct_count] = node.data_blocks[:direct_count]
    remainder = node.data_blocks[direct_count:]
    if not remainder:
        return

    single_count = min(EXT2_BLOCK_SIZE // 4, len(remainder))
    single_block = allocate_block()
    node.indirect_blocks.append(single_block)
    single_data = bytearray(EXT2_BLOCK_SIZE)
    for index, block_number in enumerate(remainder[:single_count]):
        struct.pack_into("<I", single_data, index * 4, block_number)
    write_block_callback(single_block, single_data)
    node.block_pointers[12] = single_block

    remainder = remainder[single_count:]
    if not remainder:
        return

    double_root = allocate_block()
    node.indirect_blocks.append(double_root)
    double_data = bytearray(EXT2_BLOCK_SIZE)
    for index in range(0, len(remainder), EXT2_BLOCK_SIZE // 4):
        group = remainder[index : index + EXT2_BLOCK_SIZE // 4]
        indirect_block = allocate_block()
        node.indirect_blocks.append(indirect_block)
        indirect_data = bytearray(EXT2_BLOCK_SIZE)
        for pointer_index, block_number in enumerate(group):
            struct.pack_into("<I", indirect_data, pointer_index * 4, block_number)
        write_block_callback(indirect_block, indirect_data)
        struct.pack_into("<I", double_data, (index // 256) * 4, indirect_block)
    write_block_callback(double_root, double_data)
    node.block_pointers[13] = double_root


def populate_ext2(volume, files_root, extra_files=()):
    block_count = len(volume) // EXT2_BLOCK_SIZE
    group_count = math.ceil(
        (block_count - EXT2_FIRST_DATA_BLOCK) / EXT2_BLOCKS_PER_GROUP
    )
    inode_count = group_count * EXT2_INODES_PER_GROUP
    inode_table_blocks = math.ceil(
        EXT2_INODES_PER_GROUP * EXT2_INODE_SIZE / EXT2_BLOCK_SIZE
    )
    group_descriptors = []
    used_blocks = {0}
    used_inodes = set(range(1, EXT2_FIRST_DYNAMIC_INODE))

    if (group_count * 32) > EXT2_BLOCK_SIZE:
        raise ValueError("The ext2 group descriptor table is too large.")

    root = build_directory(files_root, "/")
    root.inode = EXT2_ROOT_INODE
    root.parent_inode = EXT2_ROOT_INODE
    used_paths = set()
    for name, path in extra_files:
        # Names may contain "/" to place the file in a (created) subdirectory.
        parts = name.split("/")
        for part in parts:
            validate_name(part)
        if not path.is_file():
            raise ValueError(f"Extra disk file does not exist: {path}")
        parent = root
        for part in parts[:-1]:
            match = next(
                (child for child in parent.children if child.name == part),
                None,
            )
            if match is None:
                match = Directory(None, part)
                parent.children.append(match)
            elif not isinstance(match, Directory):
                raise ValueError(f"Extra disk path {name!r} conflicts with a file.")
            parent = match
        if name in used_paths or any(
            child.name == parts[-1] for child in parent.children
        ):
            raise ValueError(f"Extra disk file {name!r} conflicts with a workspace file.")
        used_paths.add(name)
        parent.children.append(File(path, parts[-1], path.read_bytes()))

    nodes = []
    assign_inode_children(root, EXT2_FIRST_DYNAMIC_INODE, nodes)
    nodes.insert(0, root)
    for node in nodes:
        used_inodes.add(node.inode)
        validate_name(node.name) if node is not root else None

    for group in range(group_count):
        group_start = EXT2_FIRST_DATA_BLOCK + group * EXT2_BLOCKS_PER_GROUP
        group_end = min(group_start + EXT2_BLOCKS_PER_GROUP, block_count)
        block_bitmap = group_start + 2
        inode_bitmap = group_start + 3
        inode_table = group_start + 4
        metadata_end = inode_table + inode_table_blocks
        if metadata_end > group_end:
            raise ValueError("Ext2 group metadata does not fit in the disk image.")
        group_descriptors.append(
            {
                "start": group_start,
                "end": group_end,
                "block_bitmap": block_bitmap,
                "inode_bitmap": inode_bitmap,
                "inode_table": inode_table,
                "free_blocks": 0,
                "free_inodes": 0,
                "used_directories": 0,
            }
        )
        used_blocks.update(range(group_start, metadata_end))
        if group == 0:
            used_blocks.add(group_start + 1)
        else:
            used_blocks.add(group_start)
            used_blocks.add(group_start + 1)

    next_block = EXT2_FIRST_DATA_BLOCK

    def allocate_block():
        nonlocal next_block
        while next_block < block_count and next_block in used_blocks:
            next_block += 1
        if next_block >= block_count:
            raise ValueError("The workspace exceeds the available ext2 disk space.")
        result = next_block
        used_blocks.add(result)
        next_block += 1
        return result

    def write_ext2_block(block_number, data):
        write_block(volume, block_number, data)

    for node in nodes:
        allocate_inode_blocks(node, allocate_block, write_ext2_block)

    for node in nodes:
        group = (node.inode - 1) // EXT2_INODES_PER_GROUP
        inode_index = (node.inode - 1) % EXT2_INODES_PER_GROUP
        descriptor = group_descriptors[group]
        inode_table_offset = (
            descriptor["inode_table"] * EXT2_BLOCK_SIZE
            + inode_index * EXT2_INODE_SIZE
        )
        child_directories = sum(
            isinstance(child, Directory) for child in node.children
        ) if isinstance(node, Directory) else 0
        inode_data = pack_inode(node, child_directories)
        volume[inode_table_offset : inode_table_offset + EXT2_INODE_SIZE] = (
            inode_data
        )
        if isinstance(node, Directory):
            descriptor["used_directories"] += 1

    group_descriptors_data = bytearray(EXT2_BLOCK_SIZE)
    free_blocks_total = 0
    free_inodes_total = 0
    for group, descriptor in enumerate(group_descriptors):
        group_start = descriptor["start"]
        valid_blocks = descriptor["end"] - group_start
        valid_inodes = min(
            EXT2_INODES_PER_GROUP,
            inode_count - group * EXT2_INODES_PER_GROUP,
        )
        block_bitmap = bytearray(EXT2_BLOCK_SIZE)
        for bit in range(EXT2_BLOCK_SIZE * 8):
            block_number = group_start + bit
            if bit >= valid_blocks or block_number in used_blocks:
                block_bitmap[bit // 8] |= 1 << (bit % 8)
        inode_bitmap = bytearray(EXT2_BLOCK_SIZE)
        for bit in range(EXT2_BLOCK_SIZE * 8):
            inode_number = group * EXT2_INODES_PER_GROUP + bit + 1
            if bit >= valid_inodes or inode_number in used_inodes:
                inode_bitmap[bit // 8] |= 1 << (bit % 8)

        used_block_bits = sum(
            (block_bitmap[bit // 8] >> (bit % 8)) & 1
            for bit in range(valid_blocks)
        )
        used_inode_bits = sum(
            (inode_bitmap[bit // 8] >> (bit % 8)) & 1
            for bit in range(valid_inodes)
        )
        descriptor["free_blocks"] = valid_blocks - used_block_bits
        descriptor["free_inodes"] = valid_inodes - used_inode_bits
        free_blocks_total += descriptor["free_blocks"]
        free_inodes_total += descriptor["free_inodes"]
        write_block(volume, descriptor["block_bitmap"], block_bitmap)
        write_block(volume, descriptor["inode_bitmap"], inode_bitmap)

        struct.pack_into(
            "<I I I H H H H",
            group_descriptors_data,
            group * 32,
            descriptor["block_bitmap"],
            descriptor["inode_bitmap"],
            descriptor["inode_table"],
            descriptor["free_blocks"],
            descriptor["free_inodes"],
            descriptor["used_directories"],
            0,
        )

    write_block(volume, EXT2_FIRST_DATA_BLOCK + 1, group_descriptors_data)
    for group in range(1, group_count):
        write_block(
            volume,
            group_descriptors[group]["start"] + 1,
            group_descriptors_data,
        )

    superblock = bytearray(EXT2_BLOCK_SIZE)
    struct.pack_into("<I I I I I", superblock, 0,
                     inode_count, block_count, 0, free_blocks_total,
                     free_inodes_total)
    struct.pack_into("<I", superblock, 20, EXT2_FIRST_DATA_BLOCK)
    struct.pack_into("<I", superblock, 24, 0)
    struct.pack_into("<I I I", superblock, 32,
                     EXT2_BLOCKS_PER_GROUP, EXT2_BLOCKS_PER_GROUP,
                     EXT2_INODES_PER_GROUP)
    struct.pack_into("<H", superblock, 52, 0)
    struct.pack_into("<H", superblock, 54, 0xFFFF)
    struct.pack_into("<H", superblock, 56, 0xEF53)
    struct.pack_into("<H", superblock, 58, 1)
    struct.pack_into("<I", superblock, 76, 1)
    struct.pack_into("<I", superblock, 84, EXT2_FIRST_DYNAMIC_INODE)
    struct.pack_into("<H", superblock, 88, EXT2_INODE_SIZE)
    struct.pack_into("<I", superblock, 96, EXT2_FEATURE_INCOMPAT_FILETYPE)
    superblock[104:120] = bytes.fromhex("4d696e4f532d657874322d696d616765")
    superblock[120:130] = b"MINOS EXT2"
    write_block(volume, EXT2_FIRST_DATA_BLOCK, superblock)
    for group in range(1, group_count):
        backup_superblock = bytearray(superblock)
        struct.pack_into("<H", backup_superblock, 90, group)
        write_block(
            volume,
            group_descriptors[group]["start"],
            backup_superblock,
        )

    print(
        f"Created ext2 volume with {len(root.children)} root entries, "
        f"{block_count} blocks, and {group_count} block groups."
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


def build_disk(boot_path, kernel_path, output_path, files_root, extra_files=()):
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
    populate_ext2(
        memoryview(disk)[filesystem_start:],
        files_root,
        extra_files,
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
    parser.add_argument(
        "--extra-file",
        nargs=2,
        action="append",
        default=[],
        metavar=("NAME", "PATH"),
    )
    arguments = parser.parse_args()
    build_disk(
        arguments.boot,
        arguments.kernel,
        arguments.output,
        arguments.files_root,
        [(name, Path(path)) for name, path in arguments.extra_file],
    )


if __name__ == "__main__":
    main()
