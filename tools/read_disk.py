import argparse
import struct
import sys
from pathlib import Path


BLOCK_SIZE = 1024
EXT2_SUPERBLOCK_OFFSET = 1024
EXT2_MAGIC = 0xEF53
S_IFMT = 0xF000
S_IFDIR = 0x4000
S_IFREG = 0x8000


class Ext2Image:
    def __init__(self, path):
        self.path = Path(path)
        self.image = self.path.read_bytes()
        if len(self.image) < EXT2_SUPERBLOCK_OFFSET + BLOCK_SIZE:
            raise ValueError("Disk image is too small to contain an ext2 superblock.")

        superblock = self.image[
            EXT2_SUPERBLOCK_OFFSET : EXT2_SUPERBLOCK_OFFSET + BLOCK_SIZE
        ]
        if self.u16(superblock, 56) != EXT2_MAGIC:
            raise ValueError(f"{self.path} does not contain an ext2 filesystem.")
        if self.u32(superblock, 24) != 0:
            raise ValueError("Only 1 KiB ext2 block sizes are supported.")

        self.inode_count = self.u32(superblock, 0)
        self.block_count = self.u32(superblock, 4)
        self.blocks_per_group = self.u32(superblock, 32)
        self.inodes_per_group = self.u32(superblock, 40)
        self.first_data_block = self.u32(superblock, 20)
        self.inode_size = self.u16(superblock, 88) or 128
        self.revision = self.u32(superblock, 76)
        if self.revision == 0:
            self.inode_size = 128

    @staticmethod
    def u16(data, offset):
        return struct.unpack_from("<H", data, offset)[0]

    @staticmethod
    def u32(data, offset):
        return struct.unpack_from("<I", data, offset)[0]

    def block(self, block_number):
        if block_number <= 0 or block_number >= self.block_count:
            raise ValueError(f"Invalid ext2 block number: {block_number}.")
        start = block_number * BLOCK_SIZE
        end = start + BLOCK_SIZE
        if end > len(self.image):
            raise ValueError(f"Block {block_number} is outside the disk image.")
        return self.image[start:end]

    def inode(self, inode_number):
        if inode_number <= 0 or inode_number > self.inode_count:
            raise ValueError(f"Invalid ext2 inode number: {inode_number}.")
        group = (inode_number - 1) // self.inodes_per_group
        index_in_group = (inode_number - 1) % self.inodes_per_group
        descriptor_offset = (
            (self.first_data_block + 1) * BLOCK_SIZE + group * 32
        )
        inode_table = self.u32(self.image, descriptor_offset + 8)
        offset = inode_table * BLOCK_SIZE + index_in_group * self.inode_size
        raw = self.image[offset : offset + self.inode_size]
        if len(raw) != self.inode_size:
            raise ValueError(f"Inode {inode_number} lies outside the disk image.")
        mode = self.u16(raw, 0)
        size = self.u32(raw, 4)
        if mode & S_IFMT == S_IFREG and self.inode_size >= 112:
            size |= self.u32(raw, 108) << 32
        pointers = struct.unpack_from("<15I", raw, 40)
        return mode, size, pointers

    def file_blocks(self, pointers, count):
        result = list(pointers[:12])
        remaining = count - len(result)
        if remaining > 0 and pointers[12] != 0:
            indirect = self.block(pointers[12])
            entries = min(remaining, BLOCK_SIZE // 4)
            result.extend(
                self.u32(indirect, index * 4) for index in range(entries)
            )
            remaining -= entries
        if remaining > 0 and pointers[13] != 0:
            root = self.block(pointers[13])
            while remaining > 0 and len(result) < count:
                root_index = (len(result) - 12 - (BLOCK_SIZE // 4)) // (
                    BLOCK_SIZE // 4
                )
                if root_index >= BLOCK_SIZE // 4:
                    break
                indirect_number = self.u32(root, root_index * 4)
                if indirect_number == 0:
                    result.append(0)
                    remaining -= 1
                    continue
                indirect = self.block(indirect_number)
                indirect_start = 12 + BLOCK_SIZE // 4 + root_index * (
                    BLOCK_SIZE // 4
                )
                first = max(len(result), indirect_start) - indirect_start
                entries = min(
                    remaining,
                    BLOCK_SIZE // 4 - first,
                    count - len(result),
                )
                result.extend(
                    self.u32(indirect, index * 4)
                    for index in range(first, first + entries)
                )
                remaining -= entries
        return (result + [0] * max(0, count - len(result)))[:count]

    def read_inode(self, inode_number):
        mode, size, pointers = self.inode(inode_number)
        block_count = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
        blocks = self.file_blocks(pointers, block_count)
        contents = bytearray()
        for block_number in blocks:
            if block_number == 0:
                contents.extend(bytes(BLOCK_SIZE))
            else:
                contents.extend(self.block(block_number))
        return mode, bytes(contents[:size])

    def directory_entries(self, inode_number):
        mode, contents = self.read_inode(inode_number)
        if mode & S_IFMT != S_IFDIR:
            raise ValueError("Requested path is not a directory.")
        entries = []
        offset = 0
        while offset < len(contents):
            if len(contents) - offset < 8:
                raise ValueError("Malformed ext2 directory entry.")
            entry_inode = self.u32(contents, offset)
            record_length = self.u16(contents, offset + 4)
            name_length = contents[offset + 6]
            if (
                record_length < 8
                or record_length % 4
                or record_length > len(contents) - offset
                or name_length > record_length - 8
            ):
                raise ValueError("Malformed ext2 directory entry.")
            name = contents[offset + 8 : offset + 8 + name_length].decode(
                "utf-8", errors="replace"
            )
            if entry_inode:
                entries.append((name, entry_inode))
            offset += record_length
        return entries

    def lookup(self, path):
        inode_number = 2
        components = [part for part in path.split("/") if part not in ("", ".")]
        for component in components:
            if component == "..":
                component = ".."
            entries = self.directory_entries(inode_number)
            match = next(
                (inode for name, inode in entries if name.casefold() == component.casefold()),
                None,
            )
            if match is None:
                raise FileNotFoundError(f"Path not found in ext2 image: {path}")
            inode_number = match
        return inode_number


def main():
    parser = argparse.ArgumentParser(
        description="Read files or list directories in a MinOS ext2 disk image."
    )
    parser.add_argument(
        "image",
        nargs="?",
        type=Path,
        default=(
            Path(__file__).resolve().parents[1]
            / "build"
            / "disks"
            / "data.img"
        ),
    )
    parser.add_argument("path", nargs="?", default="/")
    arguments = parser.parse_args()

    disk = Ext2Image(arguments.image)
    inode_number = disk.lookup(arguments.path)
    mode, contents = disk.read_inode(inode_number)
    if mode & S_IFMT == S_IFDIR:
        for name, _ in disk.directory_entries(inode_number):
            if name not in (".", ".."):
                print(name)
    elif mode & S_IFMT == S_IFREG:
        sys.stdout.buffer.write(contents)
    else:
        raise ValueError(f"Unsupported ext2 object at {arguments.path}.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"read_disk: {error}", file=sys.stderr)
        sys.exit(1)
