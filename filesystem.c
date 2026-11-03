#include <intrin.h>
#include "kernel.h"

#define FILESYSTEM_START_SECTOR 129
#define DISK_SECTOR_COUNT 32768
#define SECTOR_SIZE 512
#define EXT2_BLOCK_SIZE 1024
#define EXT2_SUPERBLOCK_SECTOR (FILESYSTEM_START_SECTOR + 2)
#define EXT2_SUPERBLOCK_MAGIC 0xEF53
#define EXT2_ROOT_INODE 2
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_FEATURE_COMPAT_SUPPORTED 0
#define EXT2_FEATURE_INCOMPAT_SUPPORTED EXT2_FEATURE_INCOMPAT_FILETYPE
#define EXT2_FEATURE_RO_COMPAT_SUPPORTED 0x0003
#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define EXT2_NAME_MAX 255
#define EXT2_POINTERS_PER_BLOCK (EXT2_BLOCK_SIZE / 4)
#define ATA_DATA_PORT 0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT 0x1F3
#define ATA_LBA_MID_PORT 0x1F4
#define ATA_LBA_HIGH_PORT 0x1F5
#define ATA_DRIVE_PORT 0x1F6
#define ATA_STATUS_PORT 0x1F7
#define ATA_COMMAND_PORT 0x1F7
#define ATA_ALT_STATUS_PORT 0x3F6
#define ATA_READ_SECTORS 0x20

typedef struct
{
    unsigned int inode_count;
    unsigned int block_count;
    unsigned int first_data_block;
    unsigned int blocks_per_group;
    unsigned int inodes_per_group;
    unsigned int group_count;
    unsigned int feature_incompat;
    unsigned int feature_ro_compat;
    unsigned short inode_size;
} Ext2FileSystem;

typedef struct
{
    unsigned int inode;
    unsigned int size;
    unsigned int size_high;
    unsigned char mode[2];
    unsigned char block_pointers[60];
} Ext2Inode;

typedef struct
{
    unsigned int byte_offset;
    unsigned int loaded_block;
    unsigned int block_offset;
    unsigned char block[EXT2_BLOCK_SIZE];
} Ext2DirectoryIterator;

typedef struct
{
    unsigned int inode;
    unsigned short name_length;
    unsigned char file_type;
    char name[EXT2_NAME_MAX + 1];
} Ext2DirectoryEntry;

enum FileSystemError
{
    FILESYSTEM_OK,
    FILESYSTEM_DISK_READ_ERROR,
    FILESYSTEM_INVALID_SUPERBLOCK,
    FILESYSTEM_UNSUPPORTED_FEATURE
};

static void ata_delay_400ns(void)
{
    unsigned int index;

    for (index = 0; index < 4; ++index)
    {
        __inbyte(ATA_ALT_STATUS_PORT);
    }
}

static int ata_wait_not_busy(unsigned char *last_status)
{
    unsigned int timeout;

    for (timeout = 0; timeout < 1000000; ++timeout)
    {
        unsigned char status = __inbyte(ATA_STATUS_PORT);

        if (last_status != 0)
        {
            *last_status = status;
        }

        if (status == 0 || status == 0xFF ||
            (status & 0x21) != 0)
        {
            return 0;
        }

        if ((status & 0x80) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int ata_read_sector(unsigned int lba, unsigned char *sector)
{
    unsigned int index;
    unsigned char status;

    if (lba >= DISK_SECTOR_COUNT || !ata_wait_not_busy(&status))
    {
        return 0;
    }

    __outbyte(ATA_DRIVE_PORT, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();
    __outbyte(ATA_SECTOR_COUNT_PORT, 1);
    __outbyte(ATA_LBA_LOW_PORT, (unsigned char)lba);
    __outbyte(ATA_LBA_MID_PORT, (unsigned char)(lba >> 8));
    __outbyte(ATA_LBA_HIGH_PORT, (unsigned char)(lba >> 16));
    __outbyte(ATA_COMMAND_PORT, ATA_READ_SECTORS);

    for (index = 0; index < 1000000; ++index)
    {
        status = __inbyte(ATA_STATUS_PORT);
        if (status == 0 || status == 0xFF ||
            (status & 0x21) != 0)
        {
            return 0;
        }
        if ((status & 0x88) == 0x08)
        {
            break;
        }
    }

    if (index == 1000000)
    {
        return 0;
    }

    for (index = 0; index < SECTOR_SIZE; index += 2)
    {
        unsigned short word = __inword(ATA_DATA_PORT);
        sector[index] = (unsigned char)word;
        sector[index + 1] = (unsigned char)(word >> 8);
    }

    ata_delay_400ns();
    status = __inbyte(ATA_STATUS_PORT);
    return status != 0 && status != 0xFF && (status & 0x21) == 0;
}

static unsigned short read_little_endian_word(const unsigned char *data)
{
    return (unsigned short)(data[0] | ((unsigned short)data[1] << 8));
}

static unsigned int read_little_endian_dword(const unsigned char *data)
{
    return (unsigned int)data[0] |
        ((unsigned int)data[1] << 8) |
        ((unsigned int)data[2] << 16) |
        ((unsigned int)data[3] << 24);
}

static int ext2_read_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    unsigned char block[EXT2_BLOCK_SIZE])
{
    unsigned int first_sector;

    if (block_number >= filesystem->block_count)
    {
        return 0;
    }

    first_sector =
        FILESYSTEM_START_SECTOR + block_number * (EXT2_BLOCK_SIZE / SECTOR_SIZE);
    return ata_read_sector(first_sector, block) &&
        ata_read_sector(first_sector + 1, block + SECTOR_SIZE);
}

static enum FileSystemError filesystem_mount(Ext2FileSystem *filesystem)
{
    unsigned char sector[SECTOR_SIZE];
    unsigned int revision;
    unsigned int block_size_log;
    unsigned int blocks_available;
    unsigned int inode_groups;
    unsigned int maximum_blocks;
    unsigned int compat_features;
    unsigned int incompat_features;
    unsigned int ro_compat_features;

    if (!ata_read_sector(EXT2_SUPERBLOCK_SECTOR, sector))
    {
        return FILESYSTEM_DISK_READ_ERROR;
    }

    if (read_little_endian_word(&sector[56]) != EXT2_SUPERBLOCK_MAGIC)
    {
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    filesystem->inode_count = read_little_endian_dword(&sector[0]);
    filesystem->block_count = read_little_endian_dword(&sector[4]);
    filesystem->first_data_block = read_little_endian_dword(&sector[20]);
    block_size_log = read_little_endian_dword(&sector[24]);
    filesystem->blocks_per_group = read_little_endian_dword(&sector[32]);
    filesystem->inodes_per_group = read_little_endian_dword(&sector[40]);
    revision = read_little_endian_dword(&sector[76]);
    filesystem->inode_size = revision == 0
        ? 128
        : read_little_endian_word(&sector[88]);
    compat_features = read_little_endian_dword(&sector[92]);
    incompat_features = read_little_endian_dword(&sector[96]);
    ro_compat_features = read_little_endian_dword(&sector[100]);

    if (revision > 1 || block_size_log != 0 ||
        filesystem->first_data_block != 1 ||
        filesystem->inode_count < EXT2_ROOT_INODE ||
        filesystem->block_count <= filesystem->first_data_block ||
        filesystem->blocks_per_group == 0 ||
        filesystem->blocks_per_group > EXT2_BLOCK_SIZE * 8 ||
        filesystem->inodes_per_group == 0 ||
        filesystem->inodes_per_group > EXT2_BLOCK_SIZE * 8 ||
        (filesystem->inode_size != 128 && filesystem->inode_size != 256) ||
        (filesystem->inode_size & 3) != 0)
    {
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    if ((compat_features & ~EXT2_FEATURE_COMPAT_SUPPORTED) != 0 ||
        (incompat_features & ~EXT2_FEATURE_INCOMPAT_SUPPORTED) != 0 ||
        (ro_compat_features & ~EXT2_FEATURE_RO_COMPAT_SUPPORTED) != 0)
    {
        return FILESYSTEM_UNSUPPORTED_FEATURE;
    }

    maximum_blocks =
        (DISK_SECTOR_COUNT - FILESYSTEM_START_SECTOR) /
        (EXT2_BLOCK_SIZE / SECTOR_SIZE);
    if (filesystem->block_count > maximum_blocks)
    {
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    blocks_available =
        filesystem->block_count - filesystem->first_data_block;
    filesystem->group_count =
        (blocks_available + filesystem->blocks_per_group - 1) /
        filesystem->blocks_per_group;
    inode_groups =
        (filesystem->inode_count + filesystem->inodes_per_group - 1) /
        filesystem->inodes_per_group;
    if (filesystem->group_count == 0 ||
        filesystem->group_count != inode_groups)
    {
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    filesystem->feature_incompat = incompat_features;
    filesystem->feature_ro_compat = ro_compat_features;
    return FILESYSTEM_OK;
}

static int ext2_read_group_descriptor(
    const Ext2FileSystem *filesystem,
    unsigned int group,
    unsigned char descriptor[32])
{
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int descriptor_offset;
    unsigned int descriptor_block;

    if (group >= filesystem->group_count)
    {
        return 0;
    }

    descriptor_offset = group * 32;
    descriptor_block =
        filesystem->first_data_block + 1 +
        descriptor_offset / EXT2_BLOCK_SIZE;
    if (!ext2_read_block(filesystem, descriptor_block, block))
    {
        return 0;
    }

    {
        unsigned int index;
        unsigned int offset = descriptor_offset % EXT2_BLOCK_SIZE;
        volatile unsigned char *descriptor_output = descriptor;

        for (index = 0; index < 32; ++index)
        {
            descriptor_output[index] = block[offset + index];
        }
    }
    return 1;
}

static int ext2_read_inode(
    const Ext2FileSystem *filesystem,
    unsigned int inode_number,
    Ext2Inode *inode)
{
    unsigned char descriptor[32];
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int group;
    unsigned int index_in_group;
    unsigned int table_block;
    unsigned int byte_offset;
    unsigned int index;

    if (inode_number == 0 || inode_number > filesystem->inode_count)
    {
        return 0;
    }

    group = (inode_number - 1) / filesystem->inodes_per_group;
    index_in_group = (inode_number - 1) % filesystem->inodes_per_group;
    if (!ext2_read_group_descriptor(filesystem, group, descriptor))
    {
        return 0;
    }

    table_block = read_little_endian_dword(&descriptor[8]);
    byte_offset = index_in_group * filesystem->inode_size;
    table_block += byte_offset / EXT2_BLOCK_SIZE;
    byte_offset %= EXT2_BLOCK_SIZE;
    if (!ext2_read_block(filesystem, table_block, block))
    {
        return 0;
    }

    for (index = 0; index < 60; ++index)
    {
        ((volatile unsigned char *)inode->block_pointers)[index] =
            block[byte_offset + 40 + index];
    }
    inode->inode = inode_number;
    inode->mode[0] = block[byte_offset];
    inode->mode[1] = block[byte_offset + 1];
    inode->size = read_little_endian_dword(&block[byte_offset + 4]);
    inode->size_high =
        read_little_endian_dword(&block[byte_offset + 108]);
    return 1;
}

static unsigned int ext2_inode_mode(const Ext2Inode *inode)
{
    return read_little_endian_word(inode->mode);
}

static unsigned int ext2_inode_block_pointer(
    const Ext2Inode *inode,
    unsigned int pointer_index)
{
    return read_little_endian_dword(
        &inode->block_pointers[pointer_index * 4]);
}

static int ext2_read_indirect_pointer(
    const Ext2FileSystem *filesystem,
    unsigned int pointer_block,
    unsigned int index,
    unsigned int *result)
{
    unsigned char block[EXT2_BLOCK_SIZE];

    if (pointer_block == 0 || index >= EXT2_POINTERS_PER_BLOCK ||
        !ext2_read_block(filesystem, pointer_block, block))
    {
        return 0;
    }

    *result = read_little_endian_dword(&block[index * 4]);
    return 1;
}

static int ext2_get_inode_block(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *inode,
    unsigned int logical_block,
    unsigned int *physical_block)
{
    unsigned int indirect_block;
    unsigned int first_index;
    unsigned int second_index;

    if (logical_block < 12)
    {
        *physical_block =
            ext2_inode_block_pointer(inode, logical_block);
        return 1;
    }

    logical_block -= 12;
    if (logical_block < EXT2_POINTERS_PER_BLOCK)
    {
        indirect_block = ext2_inode_block_pointer(inode, 12);
        return indirect_block == 0
            ? (*physical_block = 0, 1)
            : ext2_read_indirect_pointer(
                filesystem, indirect_block, logical_block, physical_block);
    }

    logical_block -= EXT2_POINTERS_PER_BLOCK;
    if (logical_block >=
        EXT2_POINTERS_PER_BLOCK * EXT2_POINTERS_PER_BLOCK)
    {
        return 0;
    }

    first_index = logical_block / EXT2_POINTERS_PER_BLOCK;
    second_index = logical_block % EXT2_POINTERS_PER_BLOCK;
    indirect_block = ext2_inode_block_pointer(inode, 13);
    if (indirect_block == 0)
    {
        *physical_block = 0;
        return 1;
    }
    if (!ext2_read_indirect_pointer(
        filesystem, indirect_block, first_index, &indirect_block))
    {
        return 0;
    }
    if (indirect_block == 0)
    {
        *physical_block = 0;
        return 1;
    }
    return ext2_read_indirect_pointer(
        filesystem, indirect_block, second_index, physical_block);
}

static int ext2_directory_next(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *directory,
    Ext2DirectoryIterator *iterator,
    Ext2DirectoryEntry *entry)
{
    unsigned int logical_block;
    unsigned int physical_block;
    unsigned int record_length;
    unsigned int name_length;
    unsigned int index;
    unsigned char *record;

    if (iterator->byte_offset >= directory->size)
    {
        return 0;
    }

    logical_block = iterator->byte_offset / EXT2_BLOCK_SIZE;
    iterator->block_offset = iterator->byte_offset % EXT2_BLOCK_SIZE;
    if (iterator->loaded_block != logical_block)
    {
        if (!ext2_get_inode_block(
            filesystem, directory, logical_block, &physical_block) ||
            physical_block == 0 ||
            !ext2_read_block(filesystem, physical_block, iterator->block))
        {
            return -1;
        }
        iterator->loaded_block = logical_block;
    }

    record = &iterator->block[iterator->block_offset];
    record_length = read_little_endian_word(&record[4]);
    if (record_length < 8 || (record_length & 3) != 0 ||
        record_length > EXT2_BLOCK_SIZE - iterator->block_offset)
    {
        return -1;
    }

    if ((filesystem->feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) != 0)
    {
        name_length = record[6];
        entry->file_type = record[7];
    }
    else
    {
        name_length = read_little_endian_word(&record[6]);
        entry->file_type = 0;
    }
    if (name_length > EXT2_NAME_MAX ||
        name_length > record_length - 8)
    {
        return -1;
    }

    entry->inode = read_little_endian_dword(&record[0]);
    entry->name_length = (unsigned short)name_length;
    for (index = 0; index < name_length; ++index)
    {
        entry->name[index] = (char)record[8 + index];
    }
    entry->name[name_length] = '\0';
    iterator->byte_offset += record_length;
    return 1;
}

static int ext2_name_equals(
    const char *left,
    unsigned int left_length,
    const char *right,
    unsigned int right_length)
{
    unsigned int index;

    if (left_length != right_length)
    {
        return 0;
    }

    for (index = 0; index < left_length; ++index)
    {
        char left_character = left[index];
        char right_character = right[index];

        if (left_character >= 'a' && left_character <= 'z')
        {
            left_character = (char)(left_character - 'a' + 'A');
        }
        if (right_character >= 'a' && right_character <= 'z')
        {
            right_character = (char)(right_character - 'a' + 'A');
        }
        if (left_character != right_character)
        {
            return 0;
        }
    }
    return 1;
}

static int ext2_lookup_root_file(
    const Ext2FileSystem *filesystem,
    const char *name,
    unsigned int name_length,
    Ext2Inode *file_inode)
{
    Ext2Inode root_inode;
    Ext2DirectoryIterator iterator;
    Ext2DirectoryEntry entry;
    int result;

    if (!ext2_read_inode(filesystem, EXT2_ROOT_INODE, &root_inode) ||
        (ext2_inode_mode(&root_inode) & EXT2_S_IFMT) != EXT2_S_IFDIR)
    {
        return -1;
    }

    iterator.byte_offset = 0;
    iterator.loaded_block = 0xFFFFFFFF;
    while ((result = ext2_directory_next(
        filesystem, &root_inode, &iterator, &entry)) > 0)
    {
        if (entry.inode == 0 ||
            !ext2_name_equals(
                entry.name, entry.name_length, name, name_length))
        {
            continue;
        }

        if (!ext2_read_inode(filesystem, entry.inode, file_inode))
        {
            return -1;
        }
        if ((ext2_inode_mode(file_inode) & EXT2_S_IFMT) != EXT2_S_IFREG)
        {
            return 0;
        }
        return 1;
    }

    return result < 0 ? -1 : 0;
}

static int ext2_make_executable_name(
    const char *name,
    char normalized[EXT2_NAME_MAX + 1],
    unsigned int *normalized_length)
{
    unsigned int length = 0;
    unsigned int index;
    int has_extension = 0;

    if (name == 0 || name[0] == '\0')
    {
        return 0;
    }

    while (name[length] != '\0')
    {
        if (length == EXT2_NAME_MAX || name[length] == '/')
        {
            return 0;
        }
        if (name[length] == '.')
        {
            has_extension = 1;
        }
        normalized[length] = name[length];
        ++length;
    }

    if (!has_extension)
    {
        static const char extension[] = ".EXE";

        if (length + sizeof(extension) - 1 > EXT2_NAME_MAX)
        {
            return 0;
        }
        for (index = 0; index < sizeof(extension) - 1; ++index)
        {
            normalized[length++] = extension[index];
        }
    }

    normalized[length] = '\0';
    *normalized_length = length;
    return 1;
}

void filesystem_list_root(void)
{
    Ext2FileSystem filesystem;
    Ext2Inode root_inode;
    Ext2DirectoryIterator iterator;
    Ext2DirectoryEntry entry;
    enum FileSystemError mount_result;
    unsigned int entries_seen = 0;
    int result;

    mount_result = filesystem_mount(&filesystem);
    if (mount_result == FILESYSTEM_DISK_READ_ERROR)
    {
        terminal_write("ls: could not read the ext2 filesystem from disk.\n");
        return;
    }
    if (mount_result == FILESYSTEM_UNSUPPORTED_FEATURE)
    {
        terminal_write("ls: ext2 filesystem uses unsupported features.\n");
        return;
    }
    if (mount_result != FILESYSTEM_OK ||
        !ext2_read_inode(&filesystem, EXT2_ROOT_INODE, &root_inode) ||
        (ext2_inode_mode(&root_inode) & EXT2_S_IFMT) != EXT2_S_IFDIR)
    {
        terminal_write("ls: filesystem has an invalid ext2 structure.\n");
        return;
    }

    iterator.byte_offset = 0;
    iterator.loaded_block = 0xFFFFFFFF;
    while ((result = ext2_directory_next(
        &filesystem, &root_inode, &iterator, &entry)) > 0)
    {
        unsigned int index;
        unsigned int mode = 0;

        if (entry.inode == 0 ||
            (entry.name_length == 1 && entry.name[0] == '.') ||
            (entry.name_length == 2 &&
                entry.name[0] == '.' && entry.name[1] == '.'))
        {
            continue;
        }

        if (entry.file_type == 2)
        {
            mode = EXT2_S_IFDIR;
        }
        else if (entry.file_type == 1)
        {
            mode = EXT2_S_IFREG;
        }
        else
        {
            Ext2Inode child_inode;

            if (!ext2_read_inode(&filesystem, entry.inode, &child_inode))
            {
                terminal_write("ls: could not read a directory entry inode.\n");
                return;
            }
            mode = ext2_inode_mode(&child_inode) & EXT2_S_IFMT;
        }

        if (mode == EXT2_S_IFDIR)
        {
            terminal_write("[DIR]  ");
            for (index = 0; index < entry.name_length; ++index)
            {
                terminal_write_character(entry.name[index]);
            }
            terminal_write("\n");
        }
        else if (mode == EXT2_S_IFREG)
        {
            Ext2Inode child_inode;

            if (!ext2_read_inode(&filesystem, entry.inode, &child_inode))
            {
                terminal_write("ls: could not read a file inode.\n");
                return;
            }
            for (index = 0; index < entry.name_length; ++index)
            {
                terminal_write_character(entry.name[index]);
            }
            terminal_write("  ");
            terminal_write_unsigned(child_inode.size);
            terminal_write(" bytes\n");
        }

        ++entries_seen;
    }

    if (result < 0)
    {
        terminal_write("ls: ext2 root directory is corrupt or unreadable.\n");
    }
    else if (entries_seen == 0)
    {
        terminal_write("(empty)\n");
    }
}

enum FileReadResult filesystem_read_root_file(
    const char *name,
    unsigned char *buffer,
    unsigned int buffer_size,
    unsigned int *file_size)
{
    Ext2FileSystem filesystem;
    Ext2Inode inode;
    unsigned char block[EXT2_BLOCK_SIZE];
    volatile unsigned char *output_buffer = buffer;
    char normalized_name[EXT2_NAME_MAX + 1];
    unsigned int name_length;
    unsigned int high_size;
    unsigned int block_index;
    int lookup_result;
    enum FileSystemError mount_result;

    if (buffer == 0 || file_size == 0 ||
        !ext2_make_executable_name(
            name, normalized_name, &name_length))
    {
        return FILE_READ_INVALID_NAME;
    }

    mount_result = filesystem_mount(&filesystem);
    if (mount_result == FILESYSTEM_DISK_READ_ERROR)
    {
        return FILE_READ_DISK_ERROR;
    }
    if (mount_result != FILESYSTEM_OK)
    {
        return FILE_READ_CORRUPT;
    }

    lookup_result = ext2_lookup_root_file(
        &filesystem, normalized_name, name_length, &inode);
    if (lookup_result < 0)
    {
        return FILE_READ_CORRUPT;
    }
    if (lookup_result == 0)
    {
        return FILE_READ_NOT_FOUND;
    }

    high_size = inode.size_high;
    if (high_size != 0)
    {
        return FILE_READ_TOO_LARGE;
    }
    if (inode.size > buffer_size)
    {
        return FILE_READ_TOO_LARGE;
    }
    *file_size = inode.size;

    for (block_index = 0;
        block_index * EXT2_BLOCK_SIZE < inode.size;
        ++block_index)
    {
        unsigned int physical_block;
        unsigned int bytes_to_copy =
            inode.size - block_index * EXT2_BLOCK_SIZE;
        unsigned int byte_index;

        if (bytes_to_copy > EXT2_BLOCK_SIZE)
        {
            bytes_to_copy = EXT2_BLOCK_SIZE;
        }
        if (!ext2_get_inode_block(
            &filesystem, &inode, block_index, &physical_block))
        {
            return FILE_READ_CORRUPT;
        }
        if (physical_block == 0)
        {
            for (byte_index = 0; byte_index < bytes_to_copy; ++byte_index)
            {
                output_buffer[block_index * EXT2_BLOCK_SIZE + byte_index] = 0;
            }
        }
        else
        {
            if (!ext2_read_block(&filesystem, physical_block, block))
            {
                return FILE_READ_DISK_ERROR;
            }
            for (byte_index = 0; byte_index < bytes_to_copy; ++byte_index)
            {
                output_buffer[block_index * EXT2_BLOCK_SIZE + byte_index] =
                    block[byte_index];
            }
        }
    }

    return FILE_READ_OK;
}
