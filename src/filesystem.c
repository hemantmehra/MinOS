#include <intrin.h>
#include "kernel.h"

#define FILESYSTEM_START_SECTOR 137
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
#define EXT2_DIRECTORY_ENTRY_HEADER_SIZE 8
#define EXT2_INODE_FIRST_NONRESERVED 11
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
#define ATA_WRITE_SECTORS 0x30
#define ATA_FLUSH_CACHE 0xE7

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
    unsigned int sectors;
    unsigned short links;
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
    unsigned int logical_block;
    unsigned int record_offset;
    unsigned int minimum_record_length;
    unsigned int record_length;
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

static int ata_write_sector(unsigned int lba, const unsigned char *sector)
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
    __outbyte(ATA_COMMAND_PORT, ATA_WRITE_SECTORS);

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
        unsigned short word = (unsigned short)(
            sector[index] | ((unsigned short)sector[index + 1] << 8));
        __outword(ATA_DATA_PORT, word);
    }

    ata_delay_400ns();
    if (!ata_wait_not_busy(&status))
    {
        return 0;
    }

    __outbyte(ATA_COMMAND_PORT, ATA_FLUSH_CACHE);
    ata_delay_400ns();
    return ata_wait_not_busy(&status);
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

static unsigned int filesystem_disable_interrupts(void)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }
    return flags;
}

static void filesystem_restore_interrupts(unsigned int flags)
{
    __asm
    {
        push flags
        popfd
    }
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

static int ext2_write_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    const unsigned char block[EXT2_BLOCK_SIZE])
{
    unsigned int first_sector;

    if (block_number >= filesystem->block_count)
    {
        return 0;
    }

    first_sector =
        FILESYSTEM_START_SECTOR + block_number * (EXT2_BLOCK_SIZE / SECTOR_SIZE);
    return ata_write_sector(first_sector, block) &&
        ata_write_sector(first_sector + 1, block + SECTOR_SIZE);
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
    inode->sectors = read_little_endian_dword(&block[byte_offset + 28]);
    inode->links = read_little_endian_word(&block[byte_offset + 26]);
    return 1;
}

static int ext2_write_inode(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *inode)
{
    unsigned char descriptor[32];
    unsigned char block[EXT2_BLOCK_SIZE];
    volatile unsigned char *block_output = block;
    const volatile unsigned char *pointer_input =
        (const volatile unsigned char *)inode->block_pointers;
    unsigned int group = (inode->inode - 1) / filesystem->inodes_per_group;
    unsigned int index_in_group =
        (inode->inode - 1) % filesystem->inodes_per_group;
    unsigned int table_block;
    unsigned int byte_offset;
    unsigned int index;

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

    block_output[byte_offset] = inode->mode[0];
    block_output[byte_offset + 1] = inode->mode[1];
    block_output[byte_offset + 4] = (unsigned char)inode->size;
    block_output[byte_offset + 5] = (unsigned char)(inode->size >> 8);
    block_output[byte_offset + 6] = (unsigned char)(inode->size >> 16);
    block_output[byte_offset + 7] = (unsigned char)(inode->size >> 24);
    block_output[byte_offset + 26] = (unsigned char)inode->links;
    block_output[byte_offset + 27] = (unsigned char)(inode->links >> 8);
    block_output[byte_offset + 28] = (unsigned char)inode->sectors;
    block_output[byte_offset + 29] = (unsigned char)(inode->sectors >> 8);
    block_output[byte_offset + 30] = (unsigned char)(inode->sectors >> 16);
    block_output[byte_offset + 31] = (unsigned char)(inode->sectors >> 24);
    for (index = 0; index < 60; ++index)
    {
        block_output[byte_offset + 40 + index] = pointer_input[index];
    }

    return ext2_write_block(filesystem, table_block, block);
}

static int ext2_group_has_superblock(
    const Ext2FileSystem *filesystem,
    unsigned int group)
{
    unsigned int value;

    if (group == 0 || group == 1 ||
        (filesystem->feature_ro_compat & 0x0001) == 0)
    {
        return 1;
    }

    value = group;
    while (value > 1 && value % 3 == 0)
    {
        value /= 3;
    }
    if (value == 1)
    {
        return 1;
    }

    value = group;
    while (value > 1 && value % 5 == 0)
    {
        value /= 5;
    }
    if (value == 1)
    {
        return 1;
    }

    value = group;
    while (value > 1 && value % 7 == 0)
    {
        value /= 7;
    }
    return value == 1;
}

static int ext2_write_group_descriptor(
    const Ext2FileSystem *filesystem,
    unsigned int group,
    unsigned short free_blocks,
    unsigned short free_inodes,
    unsigned short used_directories)
{
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int descriptor_offset = group * 32;
    unsigned int primary_block =
        filesystem->first_data_block + 1 +
        descriptor_offset / EXT2_BLOCK_SIZE;
    unsigned int descriptor_offset_in_block =
        descriptor_offset % EXT2_BLOCK_SIZE;
    unsigned int copy_group;

    if (group >= filesystem->group_count ||
        !ext2_read_block(filesystem, primary_block, block))
    {
        return 0;
    }

    block[descriptor_offset_in_block + 12] = (unsigned char)free_blocks;
    block[descriptor_offset_in_block + 13] =
        (unsigned char)(free_blocks >> 8);
    block[descriptor_offset_in_block + 14] = (unsigned char)free_inodes;
    block[descriptor_offset_in_block + 15] =
        (unsigned char)(free_inodes >> 8);
    block[descriptor_offset_in_block + 16] =
        (unsigned char)used_directories;
    block[descriptor_offset_in_block + 17] =
        (unsigned char)(used_directories >> 8);
    if (!ext2_write_block(filesystem, primary_block, block))
    {
        return 0;
    }

    for (copy_group = 1; copy_group < filesystem->group_count; ++copy_group)
    {
        unsigned int group_start =
            filesystem->first_data_block +
            copy_group * filesystem->blocks_per_group;
        unsigned int backup_block;

        if (!ext2_group_has_superblock(filesystem, copy_group))
        {
            continue;
        }

        backup_block = group_start + 1 +
            descriptor_offset / EXT2_BLOCK_SIZE;
        if (!ext2_read_block(filesystem, backup_block, block))
        {
            return 0;
        }
        block[descriptor_offset_in_block + 12] =
            (unsigned char)free_blocks;
        block[descriptor_offset_in_block + 13] =
            (unsigned char)(free_blocks >> 8);
        block[descriptor_offset_in_block + 14] =
            (unsigned char)free_inodes;
        block[descriptor_offset_in_block + 15] =
            (unsigned char)(free_inodes >> 8);
        block[descriptor_offset_in_block + 16] =
            (unsigned char)used_directories;
        block[descriptor_offset_in_block + 17] =
            (unsigned char)(used_directories >> 8);
        if (!ext2_write_block(filesystem, backup_block, block))
        {
            return 0;
        }
    }

    return 1;
}

static int ext2_update_superblock_counts(
    const Ext2FileSystem *filesystem,
    int block_delta,
    int inode_delta)
{
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int group;

    for (group = 0; group < filesystem->group_count; ++group)
    {
        unsigned int superblock_number;
        unsigned int free_blocks;
        unsigned int free_inodes;

        if (group != 0 &&
            !ext2_group_has_superblock(filesystem, group))
        {
            continue;
        }

        superblock_number = group == 0
            ? filesystem->first_data_block
            : filesystem->first_data_block +
                group * filesystem->blocks_per_group;
        if (!ext2_read_block(filesystem, superblock_number, block))
        {
            return 0;
        }

        free_blocks = read_little_endian_dword(&block[12]);
        free_inodes = read_little_endian_dword(&block[16]);
        if ((block_delta < 0 &&
                free_blocks < (unsigned int)(-block_delta)) ||
            (inode_delta < 0 &&
                free_inodes < (unsigned int)(-inode_delta)))
        {
            return 0;
        }

        free_blocks = (unsigned int)((int)free_blocks + block_delta);
        free_inodes = (unsigned int)((int)free_inodes + inode_delta);
        block[12] = (unsigned char)free_blocks;
        block[13] = (unsigned char)(free_blocks >> 8);
        block[14] = (unsigned char)(free_blocks >> 16);
        block[15] = (unsigned char)(free_blocks >> 24);
        block[16] = (unsigned char)free_inodes;
        block[17] = (unsigned char)(free_inodes >> 8);
        block[18] = (unsigned char)(free_inodes >> 16);
        block[19] = (unsigned char)(free_inodes >> 24);
        if (!ext2_write_block(filesystem, superblock_number, block))
        {
            return 0;
        }
    }

    return 1;
}

static int ext2_mark_allocated(
    const Ext2FileSystem *filesystem,
    unsigned int group,
    unsigned int bitmap_block,
    unsigned int bit,
    int inode_bitmap,
    int is_directory)
{
    unsigned char bitmap[EXT2_BLOCK_SIZE];
    unsigned char descriptor[32];
    unsigned short free_blocks;
    unsigned short free_inodes;
    unsigned short used_directories;

    if (bit >= EXT2_BLOCK_SIZE * 8 ||
        !ext2_read_block(filesystem, bitmap_block, bitmap) ||
        (bitmap[bit / 8] & (1U << (bit % 8))) != 0 ||
        !ext2_read_group_descriptor(filesystem, group, descriptor))
    {
        return 0;
    }

    free_blocks = read_little_endian_word(&descriptor[12]);
    free_inodes = read_little_endian_word(&descriptor[14]);
    used_directories = read_little_endian_word(&descriptor[16]);
    if (inode_bitmap)
    {
        if (free_inodes == 0 ||
            (is_directory && used_directories == 0xFFFF))
        {
            return 0;
        }
        --free_inodes;
        if (is_directory)
        {
            ++used_directories;
        }
    }
    else
    {
        if (free_blocks == 0)
        {
            return 0;
        }
        --free_blocks;
    }

    bitmap[bit / 8] |= (unsigned char)(1U << (bit % 8));
    if (!ext2_write_block(filesystem, bitmap_block, bitmap))
    {
        return 0;
    }

    if (!ext2_write_group_descriptor(
        filesystem, group, free_blocks, free_inodes, used_directories))
    {
        return 0;
    }

    return ext2_update_superblock_counts(
        filesystem, inode_bitmap ? 0 : -1, inode_bitmap ? -1 : 0);
}

static int ext2_find_free_block(
    const Ext2FileSystem *filesystem,
    unsigned int excluded_block,
    unsigned int *found_group,
    unsigned int *found_block,
    unsigned int *found_bit,
    unsigned int *found_bitmap)
{
    unsigned int group;

    for (group = 0; group < filesystem->group_count; ++group)
    {
        unsigned char descriptor[32];
        unsigned char bitmap[EXT2_BLOCK_SIZE];
        unsigned int group_start =
            filesystem->first_data_block +
            group * filesystem->blocks_per_group;
        unsigned int valid_blocks =
            filesystem->block_count - group_start;
        unsigned int bitmap_block;
        unsigned int bit;

        if (valid_blocks > filesystem->blocks_per_group)
        {
            valid_blocks = filesystem->blocks_per_group;
        }
        if (!ext2_read_group_descriptor(
            filesystem, group, descriptor))
        {
            return -1;
        }

        bitmap_block = read_little_endian_dword(&descriptor[0]);
        if (!ext2_read_block(filesystem, bitmap_block, bitmap))
        {
            return -1;
        }

        for (bit = 0; bit < valid_blocks; ++bit)
        {
            if ((bitmap[bit / 8] & (1U << (bit % 8))) == 0)
            {
                if (group_start + bit == excluded_block)
                {
                    continue;
                }
                *found_group = group;
                *found_bit = bit;
                *found_block = group_start + bit;
                *found_bitmap = bitmap_block;
                return 1;
            }
        }
    }

    return 0;
}

static int ext2_find_free_inode(
    const Ext2FileSystem *filesystem,
    unsigned int *found_group,
    unsigned int *found_inode,
    unsigned int *found_bit,
    unsigned int *found_bitmap)
{
    unsigned int group;

    for (group = 0; group < filesystem->group_count; ++group)
    {
        unsigned char descriptor[32];
        unsigned char bitmap[EXT2_BLOCK_SIZE];
        unsigned int first_inode =
            group * filesystem->inodes_per_group + 1;
        unsigned int valid_inodes =
            filesystem->inode_count - group * filesystem->inodes_per_group;
        unsigned int bitmap_block;
        unsigned int bit;

        if (valid_inodes > filesystem->inodes_per_group)
        {
            valid_inodes = filesystem->inodes_per_group;
        }
        if (!ext2_read_group_descriptor(
            filesystem, group, descriptor))
        {
            return -1;
        }

        bitmap_block = read_little_endian_dword(&descriptor[4]);
        if (!ext2_read_block(filesystem, bitmap_block, bitmap))
        {
            return -1;
        }

        for (bit = 0; bit < valid_inodes; ++bit)
        {
            unsigned int inode_number = first_inode + bit;

            if (inode_number < EXT2_INODE_FIRST_NONRESERVED)
            {
                continue;
            }
            if ((bitmap[bit / 8] & (1U << (bit % 8))) == 0)
            {
                *found_group = group;
                *found_bit = bit;
                *found_inode = inode_number;
                *found_bitmap = bitmap_block;
                return 1;
            }
        }
    }

    return 0;
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
    entry->logical_block = logical_block;
    entry->record_offset = iterator->block_offset;
    entry->minimum_record_length =
        (EXT2_DIRECTORY_ENTRY_HEADER_SIZE + name_length + 3) & ~3;
    entry->record_length = record_length;
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

typedef struct
{
    unsigned int logical_block;
    unsigned int record_offset;
    unsigned int record_length;
    unsigned int minimum_record_length;
    int unused_record;
} Ext2DirectorySlot;

static int ext2_make_directory_name(
    const char *name,
    unsigned int *name_length)
{
    unsigned int index;

    if (name == 0 || name[0] == '\0')
    {
        return 0;
    }

    for (index = 0; index <= 8; ++index)
    {
        char character = name[index];

        if (character == '\0')
        {
            if (index == 0)
            {
                return 0;
            }
            *name_length = index;
            return 1;
        }

        if (index == 8 ||
            !((character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '-'))
        {
            return 0;
        }
    }

    return 0;
}

static int ext2_find_root_directory_slot(
    const Ext2FileSystem *filesystem,
    const Ext2Inode *root_inode,
    const char *name,
    unsigned int name_length,
    Ext2DirectorySlot *slot)
{
    Ext2DirectoryIterator iterator;
    Ext2DirectoryEntry entry;
    unsigned int required_length =
        (EXT2_DIRECTORY_ENTRY_HEADER_SIZE + name_length + 3) & ~3;
    int have_slot = 0;
    int result;

    iterator.byte_offset = 0;
    iterator.loaded_block = 0xFFFFFFFF;
    while ((result = ext2_directory_next(
        filesystem, root_inode, &iterator, &entry)) > 0)
    {
        if (entry.inode != 0 &&
            ext2_name_equals(
                entry.name, entry.name_length, name, name_length))
        {
            return 0;
        }

        if (have_slot)
        {
            continue;
        }

        if (entry.inode == 0 && entry.record_length >= required_length)
        {
            slot->logical_block = entry.logical_block;
            slot->record_offset = entry.record_offset;
            slot->record_length = entry.record_length;
            slot->minimum_record_length = 0;
            slot->unused_record = 1;
            have_slot = 1;
        }
        else if (entry.inode != 0 &&
            entry.record_length >= entry.minimum_record_length &&
            entry.record_length - entry.minimum_record_length >=
                required_length)
        {
            slot->logical_block = entry.logical_block;
            slot->record_offset = entry.record_offset;
            slot->record_length = entry.record_length;
            slot->minimum_record_length = entry.minimum_record_length;
            slot->unused_record = 0;
            have_slot = 1;
        }
    }

    if (result < 0)
    {
        return -1;
    }
    return have_slot ? 1 : 2;
}

static void ext2_write_directory_record(
    unsigned char *block,
    unsigned int offset,
    unsigned int inode,
    unsigned int record_length,
    const char *name,
    unsigned int name_length,
    unsigned char file_type)
{
    unsigned int index;
    volatile unsigned char *block_output = block;

    for (index = 0; index < record_length; ++index)
    {
        block_output[offset + index] = 0;
    }

    block_output[offset] = (unsigned char)inode;
    block_output[offset + 1] = (unsigned char)(inode >> 8);
    block_output[offset + 2] = (unsigned char)(inode >> 16);
    block_output[offset + 3] = (unsigned char)(inode >> 24);
    block_output[offset + 4] = (unsigned char)record_length;
    block_output[offset + 5] = (unsigned char)(record_length >> 8);
    if ((EXT2_FEATURE_INCOMPAT_SUPPORTED &
            EXT2_FEATURE_INCOMPAT_FILETYPE) != 0)
    {
        block_output[offset + 6] = (unsigned char)name_length;
        block_output[offset + 7] = file_type;
    }
    else
    {
        block_output[offset + 6] = (unsigned char)name_length;
        block_output[offset + 7] = 0;
    }

    for (index = 0; index < name_length; ++index)
    {
        block_output[offset + EXT2_DIRECTORY_ENTRY_HEADER_SIZE + index] =
            (unsigned char)name[index];
    }
}

static int ext2_add_root_directory_entry(
    const Ext2FileSystem *filesystem,
    Ext2Inode *root_inode,
    const Ext2DirectorySlot *slot,
    unsigned int child_inode,
    const char *name,
    unsigned int name_length)
{
    unsigned char block[EXT2_BLOCK_SIZE];
    unsigned int physical_block;
    unsigned int entry_offset;
    unsigned int entry_length;

    if (!ext2_get_inode_block(
        filesystem, root_inode, slot->logical_block, &physical_block) ||
        physical_block == 0 ||
        !ext2_read_block(filesystem, physical_block, block))
    {
        return 0;
    }

    if (slot->unused_record)
    {
        entry_offset = slot->record_offset;
        entry_length = slot->record_length;
    }
    else
    {
        unsigned int old_length = slot->minimum_record_length;
        unsigned int old_offset = slot->record_offset;
        unsigned int remaining_length =
            slot->record_length - old_length;

        block[old_offset + 4] = (unsigned char)old_length;
        block[old_offset + 5] = (unsigned char)(old_length >> 8);
        entry_offset = old_offset + old_length;
        entry_length = remaining_length;
    }

    ext2_write_directory_record(
        block, entry_offset, child_inode, entry_length,
        name, name_length, 2);
    return ext2_write_block(filesystem, physical_block, block);
}

static int ext2_initialize_new_directory(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    unsigned int inode_number,
    unsigned int parent_inode)
{
    unsigned char block[EXT2_BLOCK_SIZE];

    ext2_write_directory_record(
        block, 0, inode_number, 12, ".", 1, 2);
    ext2_write_directory_record(
        block, 12, parent_inode, EXT2_BLOCK_SIZE - 12, "..", 2, 2);
    return ext2_write_block(filesystem, block_number, block);
}

static void ext2_initialize_directory_inode(
    Ext2Inode *inode,
    unsigned int inode_number,
    unsigned int block_number)
{
    unsigned int index;
    volatile unsigned char *pointers =
        (volatile unsigned char *)inode->block_pointers;

    inode->inode = inode_number;
    inode->size = EXT2_BLOCK_SIZE;
    inode->size_high = 0;
    inode->sectors = EXT2_BLOCK_SIZE / SECTOR_SIZE;
    inode->links = 2;
    inode->mode[0] = (unsigned char)(EXT2_S_IFDIR | 0755);
    inode->mode[1] = (unsigned char)((EXT2_S_IFDIR | 0755) >> 8);
    for (index = 0; index < 60; ++index)
    {
        pointers[index] = 0;
    }
    pointers[0] = (unsigned char)block_number;
    pointers[1] = (unsigned char)(block_number >> 8);
    pointers[2] = (unsigned char)(block_number >> 16);
    pointers[3] = (unsigned char)(block_number >> 24);
}

static enum CreateDirectoryResult filesystem_create_root_directory_locked(
    const char *name)
{
    Ext2FileSystem filesystem;
    Ext2Inode root_inode;
    Ext2Inode new_inode;
    Ext2DirectorySlot slot;
    unsigned int name_length;
    unsigned int inode_group;
    unsigned int inode_number;
    unsigned int inode_bit;
    unsigned int inode_bitmap;
    unsigned int block_group;
    unsigned int block_number;
    unsigned int block_bit;
    unsigned int block_bitmap;
    unsigned int root_block_group = 0;
    unsigned int root_block_number = 0;
    unsigned int root_block_bit = 0;
    unsigned int root_block_bitmap = 0;
    int slot_result;
    int inode_result;
    int block_result;
    enum FileSystemError mount_result;

    if (!ext2_make_directory_name(name, &name_length))
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }

    mount_result = filesystem_mount(&filesystem);
    if (mount_result == FILESYSTEM_DISK_READ_ERROR)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (mount_result != FILESYSTEM_OK)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }

    if ((filesystem.feature_incompat &
            EXT2_FEATURE_INCOMPAT_FILETYPE) == 0)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }
    if (!ext2_read_inode(&filesystem, EXT2_ROOT_INODE, &root_inode) ||
        (ext2_inode_mode(&root_inode) & EXT2_S_IFMT) != EXT2_S_IFDIR)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }

    slot_result = ext2_find_root_directory_slot(
        &filesystem, &root_inode, name, name_length, &slot);
    if (slot_result == 0)
    {
        return CREATE_DIRECTORY_EXISTS;
    }
    if (slot_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (slot_result == 2)
    {
        unsigned int directory_blocks =
            root_inode.size / EXT2_BLOCK_SIZE;

        if (root_inode.size % EXT2_BLOCK_SIZE != 0 ||
            directory_blocks >= 12)
        {
            return CREATE_DIRECTORY_ROOT_FULL;
        }

        slot.logical_block = directory_blocks;
        slot.record_offset = 0;
        slot.record_length = EXT2_BLOCK_SIZE;
        slot.minimum_record_length = 0;
        slot.unused_record = 1;
    }
    if (root_inode.links == 0xFFFF)
    {
        return CREATE_DIRECTORY_ROOT_FULL;
    }

    inode_result = ext2_find_free_inode(
        &filesystem, &inode_group, &inode_number,
        &inode_bit, &inode_bitmap);
    if (inode_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (inode_result == 0)
    {
        return CREATE_DIRECTORY_DISK_FULL;
    }

    block_result = ext2_find_free_block(
        &filesystem, 0, &block_group, &block_number,
        &block_bit, &block_bitmap);
    if (block_result < 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (block_result == 0)
    {
        return CREATE_DIRECTORY_DISK_FULL;
    }
    if (slot_result == 2)
    {
        block_result = ext2_find_free_block(
            &filesystem, block_number, &root_block_group,
            &root_block_number, &root_block_bit, &root_block_bitmap);
        if (block_result < 0)
        {
            return CREATE_DIRECTORY_IO_ERROR;
        }
        if (block_result == 0)
        {
            return CREATE_DIRECTORY_DISK_FULL;
        }
    }

    if (!ext2_initialize_new_directory(
        &filesystem, block_number, inode_number, EXT2_ROOT_INODE))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (!ext2_mark_allocated(
        &filesystem, block_group, block_bitmap, block_bit, 0, 0) ||
        (slot_result == 2 &&
            !ext2_mark_allocated(
                &filesystem, root_block_group, root_block_bitmap,
                root_block_bit, 0, 0)) ||
        !ext2_mark_allocated(
            &filesystem, inode_group, inode_bitmap, inode_bit, 1, 1))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    ext2_initialize_directory_inode(&new_inode, inode_number, block_number);
    if (!ext2_write_inode(&filesystem, &new_inode))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    if (slot_result == 2)
    {
        unsigned int logical_block = slot.logical_block;
        unsigned char block[EXT2_BLOCK_SIZE];

        if (logical_block >= 12)
        {
            return CREATE_DIRECTORY_ROOT_FULL;
        }
        ext2_write_directory_record(
            block, 0, inode_number, EXT2_BLOCK_SIZE,
            name, name_length, 2);
        if (!ext2_write_block(&filesystem, root_block_number, block))
        {
            return CREATE_DIRECTORY_IO_ERROR;
        }

        root_inode.block_pointers[logical_block * 4] =
            (unsigned char)root_block_number;
        root_inode.block_pointers[logical_block * 4 + 1] =
            (unsigned char)(root_block_number >> 8);
        root_inode.block_pointers[logical_block * 4 + 2] =
            (unsigned char)(root_block_number >> 16);
        root_inode.block_pointers[logical_block * 4 + 3] =
            (unsigned char)(root_block_number >> 24);
        root_inode.size += EXT2_BLOCK_SIZE;
        root_inode.sectors += EXT2_BLOCK_SIZE / SECTOR_SIZE;
    }
    else if (!ext2_add_root_directory_entry(
        &filesystem, &root_inode, &slot, inode_number,
        name, name_length))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    ++root_inode.links;
    if (!ext2_write_inode(&filesystem, &root_inode))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    return CREATE_DIRECTORY_OK;
}

enum CreateDirectoryResult filesystem_create_root_directory(
    const char *name)
{
    unsigned int flags = filesystem_disable_interrupts();
    enum CreateDirectoryResult result =
        filesystem_create_root_directory_locked(name);

    filesystem_restore_interrupts(flags);
    return result;
}
