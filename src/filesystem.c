#include <intrin.h>
#include "filesystem_internal.h"
#include "kernel_log.h"

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
    unsigned int owner;
    int active;
    Ext2FileSystem filesystem;
    Ext2Inode inode;
    Ext2DirectoryIterator iterator;
} Ext2DirectoryStream;

typedef struct
{
    unsigned int owner;
    unsigned int position;
    int active;
    Ext2FileSystem filesystem;
    Ext2Inode inode;
} Ext2FileStream;

static Ext2DirectoryStream directory_streams[EXT2_DIRECTORY_STREAM_LIMIT];
static Ext2FileStream file_streams[EXT2_FILE_STREAM_LIMIT];
static char current_directory_path[FILESYSTEM_PATH_MAX + 1] = "A:/";
static unsigned int filesystem_logged_mounts;

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

static int ata_read_sector(
    unsigned int drive,
    unsigned int lba,
    unsigned char *sector)
{
    unsigned int index;
    unsigned char status;
    unsigned int sector_count = drive == 0
        ? DISK_SECTOR_COUNT
        : DATA_DISK_SECTOR_COUNT;

    if (drive > 1 || lba >= sector_count ||
        !ata_wait_not_busy(&status))
    {
        return 0;
    }

    __outbyte(
        ATA_DRIVE_PORT,
        (unsigned char)(0xE0 | (drive << 4) | ((lba >> 24) & 0x0F)));
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

static int ata_write_sector(
    unsigned int drive,
    unsigned int lba,
    const unsigned char *sector)
{
    unsigned int index;
    unsigned char status;
    unsigned int sector_count = drive == 0
        ? DISK_SECTOR_COUNT
        : DATA_DISK_SECTOR_COUNT;

    if (drive > 1 || lba >= sector_count ||
        !ata_wait_not_busy(&status))
    {
        return 0;
    }

    __outbyte(
        ATA_DRIVE_PORT,
        (unsigned char)(0xE0 | (drive << 4) | ((lba >> 24) & 0x0F)));
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

int ext2_read_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    unsigned char block[EXT2_BLOCK_SIZE])
{
    unsigned int first_sector;

    if (block_number >= filesystem->block_count)
    {
        return 0;
    }

    first_sector = filesystem->start_sector +
        block_number * (EXT2_BLOCK_SIZE / SECTOR_SIZE);
    return ata_read_sector(filesystem->drive, first_sector, block) &&
        ata_read_sector(
            filesystem->drive, first_sector + 1, block + SECTOR_SIZE);
}

int ext2_write_block(
    const Ext2FileSystem *filesystem,
    unsigned int block_number,
    const unsigned char block[EXT2_BLOCK_SIZE])
{
    unsigned int first_sector;

    if (block_number >= filesystem->block_count)
    {
        return 0;
    }

    first_sector = filesystem->start_sector +
        block_number * (EXT2_BLOCK_SIZE / SECTOR_SIZE);
    return ata_write_sector(filesystem->drive, first_sector, block) &&
        ata_write_sector(
            filesystem->drive, first_sector + 1, block + SECTOR_SIZE);
}

enum FileSystemError filesystem_mount_drive(
    Ext2FileSystem *filesystem,
    unsigned int drive)
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

    if (drive > 1)
    {
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    filesystem->drive = drive;
    filesystem->start_sector = drive == 0 ? FILESYSTEM_START_SECTOR : 0;
    filesystem->sector_count =
        drive == 0 ? DISK_SECTOR_COUNT : DATA_DISK_SECTOR_COUNT;
    if (!ata_read_sector(
        filesystem->drive, filesystem->start_sector + 2, sector))
    {
        kernel_log_value(
            KERNEL_LOG_ERROR,
            "filesystem",
            "superblock read failed for drive",
            drive);
        return FILESYSTEM_DISK_READ_ERROR;
    }

    if (read_little_endian_word(&sector[56]) != EXT2_SUPERBLOCK_MAGIC)
    {
        kernel_log_value(
            KERNEL_LOG_WARNING,
            "filesystem",
            "invalid ext2 superblock on drive",
            drive);
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
        kernel_log_value(
            KERNEL_LOG_WARNING,
            "filesystem",
            "invalid ext2 geometry on drive",
            drive);
        return FILESYSTEM_INVALID_SUPERBLOCK;
    }

    if ((compat_features & ~EXT2_FEATURE_COMPAT_SUPPORTED) != 0 ||
        (incompat_features & ~EXT2_FEATURE_INCOMPAT_SUPPORTED) != 0 ||
        (ro_compat_features & ~EXT2_FEATURE_RO_COMPAT_SUPPORTED) != 0)
    {
        kernel_log_value(
            KERNEL_LOG_WARNING,
            "filesystem",
            "unsupported ext2 features on drive",
            drive);
        return FILESYSTEM_UNSUPPORTED_FEATURE;
    }

    maximum_blocks =
        (filesystem->sector_count - filesystem->start_sector) /
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
    if ((filesystem_logged_mounts & (1u << drive)) == 0)
    {
        filesystem_logged_mounts |= 1u << drive;
        kernel_log_value(
            KERNEL_LOG_INFO,
            "filesystem",
            "mounted ext2 drive",
            drive);
    }
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

int ext2_write_inode(
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

int ext2_mark_allocated(
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

int ext2_find_free_block(
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

int ext2_find_free_inode(
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

int ext2_get_inode_block(
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

int ext2_directory_next(
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

int ext2_name_equals(
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

int filesystem_prepare_path(
    const char *path,
    char canonical[FILESYSTEM_PATH_MAX + 1],
    char normalized[FILESYSTEM_PATH_MAX + 1],
    unsigned int *drive)
{
    const char *remainder;
    unsigned int output_length = 3;
    unsigned int input_length = 0;
    unsigned int index;
    int absolute = 0;
    char drive_letter;

    if (path == 0)
    {
        return 0;
    }

    if (path[0] == '\0')
    {
        path = current_directory_path;
    }

    while (path[input_length] != '\0')
    {
        if (input_length == FILESYSTEM_PATH_MAX)
        {
            return 0;
        }
        ++input_length;
    }

    drive_letter = current_directory_path[0];
    if (path[0] >= 'a' && path[0] <= 'z' &&
        path[1] == ':' && (path[2] == '/' || path[2] == '\0'))
    {
        drive_letter = (char)(path[0] - 'a' + 'A');
        remainder = path + 2;
        if (*remainder == '/')
        {
            ++remainder;
        }
        absolute = 1;
    }
    else if (path[0] >= 'A' && path[0] <= 'Z' &&
        path[1] == ':' && (path[2] == '/' || path[2] == '\0'))
    {
        drive_letter = path[0];
        remainder = path + 2;
        if (*remainder == '/')
        {
            ++remainder;
        }
        absolute = 1;
    }
    else
    {
        remainder = path;
        if (path[0] == '/')
        {
            absolute = 1;
            while (*remainder == '/')
            {
                ++remainder;
            }
        }
        else if ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z'))
        {
            if (path[1] == ':')
            {
                return 0;
            }
        }
    }

    if (drive_letter != 'A' && drive_letter != 'B')
    {
        return 0;
    }
    canonical[0] = drive_letter;
    canonical[1] = ':';
    canonical[2] = '/';
    canonical[3] = '\0';

    if (!absolute)
    {
        for (index = 3; current_directory_path[index] != '\0'; ++index)
        {
            if (output_length >= FILESYSTEM_PATH_MAX)
            {
                return 0;
            }
            canonical[output_length++] = current_directory_path[index];
        }
        canonical[output_length] = '\0';
    }

    while (*remainder != '\0')
    {
        unsigned int component_length;
        const char *component_start;

        while (*remainder == '/')
        {
            ++remainder;
        }
        if (*remainder == '\0')
        {
            break;
        }
        component_start = remainder;
        while (*remainder != '\0' && *remainder != '/')
        {
            ++remainder;
        }
        component_length = (unsigned int)(remainder - component_start);
        if (component_length == 1 && component_start[0] == '.')
        {
            continue;
        }
        if (component_length == 2 && component_start[0] == '.' &&
            component_start[1] == '.')
        {
            if (output_length > 3)
            {
                while (output_length > 3 &&
                    canonical[output_length - 1] != '/')
                {
                    --output_length;
                }
                if (output_length > 3)
                {
                    --output_length;
                }
                canonical[output_length] = '\0';
            }
            continue;
        }
        if (component_length > EXT2_NAME_MAX ||
            output_length + (output_length > 3 ? 1 : 0) +
                component_length > FILESYSTEM_PATH_MAX)
        {
            return 0;
        }
        if (output_length > 3)
        {
            canonical[output_length++] = '/';
        }
        for (index = 0; index < component_length; ++index)
        {
            canonical[output_length++] = component_start[index];
        }
        canonical[output_length] = '\0';
    }

    *drive = drive_letter == 'B' ? 1 : 0;
    normalized[0] = '/';
    for (index = 3; index < output_length; ++index)
    {
        normalized[index - 2] = canonical[index];
    }
    normalized[output_length - 2] = '\0';
    return 1;
}

static void ext2_copy_inode(
    Ext2Inode *destination,
    const Ext2Inode *source)
{
    volatile unsigned char *target =
        (volatile unsigned char *)destination;
    const volatile unsigned char *origin =
        (const volatile unsigned char *)source;
    unsigned int index;

    for (index = 0; index < sizeof(Ext2Inode); ++index)
    {
        target[index] = origin[index];
    }
}

int ext2_lookup_directory_path(
    const Ext2FileSystem *filesystem,
    const char *path,
    Ext2Inode *directory_inode)
{
    unsigned int position = 1;

    if (!ext2_read_inode(
        filesystem, EXT2_ROOT_INODE, directory_inode) ||
        (ext2_inode_mode(directory_inode) & EXT2_S_IFMT) != EXT2_S_IFDIR)
    {
        return 0;
    }

    while (path[position] != '\0')
    {
        Ext2DirectoryIterator iterator;
        Ext2DirectoryEntry entry;
        Ext2Inode child_inode;
        unsigned int component_start = position;
        unsigned int component_length;
        int result;
        int found = 0;

        while (path[position] != '\0' && path[position] != '/')
        {
            ++position;
        }
        component_length = position - component_start;
        if (path[position] == '/')
        {
            ++position;
        }
        iterator.byte_offset = 0;
        iterator.loaded_block = 0xFFFFFFFF;
        while ((result = ext2_directory_next(
            filesystem, directory_inode, &iterator, &entry)) > 0)
        {
            if (entry.inode != 0 &&
                ext2_name_equals(
                    entry.name,
                    entry.name_length,
                    path + component_start,
                    component_length))
            {
                if (!ext2_read_inode(
                    filesystem, entry.inode, &child_inode) ||
                    (ext2_inode_mode(&child_inode) & EXT2_S_IFMT) !=
                        EXT2_S_IFDIR)
                {
                    return 0;
                }
                ext2_copy_inode(directory_inode, &child_inode);
                found = 1;
                break;
            }
        }
        if (result < 0 || !found)
        {
            return 0;
        }
    }
    return 1;
}

static int ext2_lookup_file_path(
    const Ext2FileSystem *filesystem,
    const char *path,
    Ext2Inode *file_inode)
{
    char parent_path[FILESYSTEM_PATH_MAX + 1];
    volatile char *parent_output = parent_path;
    unsigned int path_length = 0;
    unsigned int last_separator = 0;
    unsigned int name_length;
    unsigned int index;
    Ext2Inode parent_inode;
    Ext2DirectoryIterator iterator;
    Ext2DirectoryEntry entry;
    int result;

    while (path[path_length] != '\0')
    {
        ++path_length;
    }
    if (path_length <= 1)
    {
        return 0;
    }
    for (index = 1; index < path_length; ++index)
    {
        if (path[index] == '/')
        {
            last_separator = index;
        }
    }
    name_length = path_length - last_separator - 1;
    if (name_length == 0)
    {
        return 0;
    }

    for (index = 0; index < last_separator; ++index)
    {
        parent_output[index] = path[index];
    }
    if (last_separator == 0)
    {
        parent_output[0] = '/';
        parent_output[1] = '\0';
    }
    else
    {
        parent_output[last_separator] = '\0';
    }
    if (!ext2_lookup_directory_path(
        filesystem, parent_path, &parent_inode))
    {
        return 0;
    }

    iterator.byte_offset = 0;
    iterator.loaded_block = 0xFFFFFFFF;
    while ((result = ext2_directory_next(
        filesystem, &parent_inode, &iterator, &entry)) > 0)
    {
        if (entry.inode == 0 ||
            !ext2_name_equals(
                entry.name,
                entry.name_length,
                path + last_separator + 1,
                name_length))
        {
            continue;
        }
        if (!ext2_read_inode(filesystem, entry.inode, file_inode) ||
            (ext2_inode_mode(file_inode) & EXT2_S_IFMT) != EXT2_S_IFREG)
        {
            return 0;
        }
        return 1;
    }
    return 0;
}

int filesystem_open_directory(unsigned int owner, const char *path)
{
    unsigned int index;
    unsigned int drive;
    enum FileSystemError mount_result;
    Ext2DirectoryStream *stream = 0;
    char canonical_path[FILESYSTEM_PATH_MAX + 1];
    char normalized_path[FILESYSTEM_PATH_MAX + 1];

    if (!filesystem_prepare_path(
        path, canonical_path, normalized_path, &drive))
    {
        return -1;
    }

    for (index = 0; index < EXT2_DIRECTORY_STREAM_LIMIT; ++index)
    {
        if (!directory_streams[index].active)
        {
            stream = &directory_streams[index];
            break;
        }
    }
    if (stream == 0)
    {
        return -1;
    }

    mount_result = filesystem_mount_drive(&stream->filesystem, drive);
    if (mount_result != FILESYSTEM_OK ||
        !ext2_lookup_directory_path(
            &stream->filesystem, normalized_path, &stream->inode))
    {
        return -1;
    }

    stream->owner = owner;
    stream->iterator.byte_offset = 0;
    stream->iterator.loaded_block = 0xFFFFFFFF;
    stream->active = 1;
    return (int)(index + 1);
}

int filesystem_change_directory(const char *path)
{
    Ext2FileSystem filesystem;
    Ext2Inode directory_inode;
    unsigned int drive;
    char canonical_path[FILESYSTEM_PATH_MAX + 1];
    char normalized_path[FILESYSTEM_PATH_MAX + 1];

    if (!filesystem_prepare_path(
            path, canonical_path, normalized_path, &drive) ||
        filesystem_mount_drive(&filesystem, drive) != FILESYSTEM_OK ||
        !ext2_lookup_directory_path(
            &filesystem, normalized_path, &directory_inode))
    {
        return 0;
    }

    {
        unsigned int index = 0;

        do
        {
            current_directory_path[index] = canonical_path[index];
        } while (canonical_path[index++] != '\0');
    }
    return 1;
}

int filesystem_get_current_directory(char *path, unsigned int capacity)
{
    unsigned int length = 0;
    unsigned int index;

    if (path == 0 || capacity == 0)
    {
        return -1;
    }
    while (current_directory_path[length] != '\0')
    {
        ++length;
    }
    if (length + 1 > capacity)
    {
        return -1;
    }
    for (index = 0; index <= length; ++index)
    {
        path[index] = current_directory_path[index];
    }
    return (int)length;
}

int filesystem_read_directory(
    unsigned int owner,
    unsigned int stream_handle,
    unsigned int *name,
    unsigned int capacity)
{
    Ext2DirectoryStream *stream;
    Ext2DirectoryEntry entry;
    int result;
    unsigned int index;

    if (stream_handle == 0 ||
        stream_handle > EXT2_DIRECTORY_STREAM_LIMIT ||
        name == 0 || capacity == 0)
    {
        return -1;
    }

    stream = &directory_streams[stream_handle - 1];
    if (!stream->active || stream->owner != owner)
    {
        return -1;
    }

    while ((result = ext2_directory_next(
        &stream->filesystem, &stream->inode, &stream->iterator, &entry)) > 0)
    {
        if (entry.inode == 0)
        {
            continue;
        }
        if (entry.name_length >= capacity)
        {
            return -1;
        }

        for (index = 0; index < entry.name_length; ++index)
        {
            name[index] = (unsigned char)entry.name[index];
        }
        name[entry.name_length] = 0;
        return 1;
    }

    return result < 0 ? -1 : 0;
}

int filesystem_close_directory(unsigned int owner, unsigned int stream_handle)
{
    Ext2DirectoryStream *stream;

    if (stream_handle == 0 ||
        stream_handle > EXT2_DIRECTORY_STREAM_LIMIT)
    {
        return -1;
    }

    stream = &directory_streams[stream_handle - 1];
    if (!stream->active || stream->owner != owner)
    {
        return -1;
    }

    stream->active = 0;
    return 0;
}

void filesystem_close_process_directories(unsigned int owner)
{
    unsigned int index;

    for (index = 0; index < EXT2_DIRECTORY_STREAM_LIMIT; ++index)
    {
        if (directory_streams[index].active &&
            directory_streams[index].owner == owner)
        {
            directory_streams[index].active = 0;
        }
    }
}

int filesystem_open_file(unsigned int owner, const char *path)
{
    unsigned int index;
    unsigned int drive;
    enum FileSystemError mount_result;
    Ext2FileStream *stream = 0;
    char canonical_path[FILESYSTEM_PATH_MAX + 1];
    char normalized_path[FILESYSTEM_PATH_MAX + 1];

    if (!filesystem_prepare_path(
        path, canonical_path, normalized_path, &drive))
    {
        return -1;
    }

    for (index = 0; index < EXT2_FILE_STREAM_LIMIT; ++index)
    {
        if (!file_streams[index].active)
        {
            stream = &file_streams[index];
            break;
        }
    }
    if (stream == 0)
    {
        return -1;
    }

    mount_result = filesystem_mount_drive(&stream->filesystem, drive);
    if (mount_result != FILESYSTEM_OK ||
        !ext2_lookup_file_path(
            &stream->filesystem, normalized_path, &stream->inode) ||
        stream->inode.size_high != 0)
    {
        return -1;
    }

    stream->owner = owner;
    stream->position = 0;
    stream->active = 1;
    return (int)(index + 1);
}

// Allocates a data block for a hole among the 12 direct pointers of an
// open file and records it in the in-memory inode (the caller writes it).
static int ext2_allocate_file_block(
    const Ext2FileSystem *filesystem,
    Ext2Inode *inode,
    unsigned int logical_block,
    unsigned int *physical_block)
{
    unsigned int group;
    unsigned int number;
    unsigned int bit;
    unsigned int bitmap;
    unsigned int index;

    if (logical_block >= 12 ||
        ext2_find_free_block(
            filesystem, 0, &group, &number, &bit, &bitmap) <= 0 ||
        !ext2_mark_allocated(filesystem, group, bitmap, bit, 0, 0))
    {
        return 0;
    }
    for (index = 0; index < 4; ++index)
    {
        inode->block_pointers[logical_block * 4 + index] =
            (unsigned char)(number >> (8 * index));
    }
    inode->sectors += EXT2_BLOCK_SIZE / SECTOR_SIZE;
    *physical_block = number;
    return 1;
}

int filesystem_write_file(
    unsigned int owner,
    unsigned int handle,
    const unsigned int *buffer,
    unsigned int length)
{
    Ext2FileStream *stream;
    unsigned int bytes_written = 0;
    unsigned int position;
    unsigned int new_size;
    int allocated = 0;

    if (handle == 0 || handle > EXT2_FILE_STREAM_LIMIT ||
        (buffer == 0 && length != 0) || length > 256)
    {
        return -1;
    }
    stream = &file_streams[handle - 1];
    if (!stream->active || stream->owner != owner ||
        stream->filesystem.drive != 1)
    {
        return -1;
    }
    if (length == 0)
    {
        return 0;
    }
    if (stream->position > 0xFFFFFFFF - length)
    {
        return -1;
    }

    position = stream->position;
    while (bytes_written < length)
    {
        unsigned char block[EXT2_BLOCK_SIZE];
        unsigned int logical_block = position / EXT2_BLOCK_SIZE;
        unsigned int block_offset = position % EXT2_BLOCK_SIZE;
        unsigned int chunk_size = EXT2_BLOCK_SIZE - block_offset;
        unsigned int physical_block;
        unsigned int index;

        if (chunk_size > length - bytes_written)
        {
            chunk_size = length - bytes_written;
        }
        if (!ext2_get_inode_block(
                &stream->filesystem, &stream->inode, logical_block,
                &physical_block))
        {
            break;
        }
        if (physical_block == 0)
        {
            if (!ext2_allocate_file_block(
                    &stream->filesystem, &stream->inode, logical_block,
                    &physical_block))
            {
                break;
            }
            allocated = 1;
            for (index = 0; index < EXT2_BLOCK_SIZE; ++index)
            {
                ((volatile unsigned char *)block)[index] = 0;
            }
        }
        else if (!ext2_read_block(
                &stream->filesystem, physical_block, block))
        {
            break;
        }
        for (index = 0; index < chunk_size; ++index)
        {
            block[block_offset + index] =
                (unsigned char)buffer[bytes_written + index];
        }
        if (!ext2_write_block(
                &stream->filesystem, physical_block, block))
        {
            break;
        }
        bytes_written += chunk_size;
        position += chunk_size;
    }

    if (bytes_written == 0 && !allocated)
    {
        return -1;
    }
    stream->position = position;
    new_size = position > stream->inode.size
        ? position
        : stream->inode.size;
    if (new_size != stream->inode.size || allocated)
    {
        stream->inode.size = new_size;
        if (!ext2_write_inode(&stream->filesystem, &stream->inode))
        {
            return -1;
        }
    }
    return bytes_written == 0 ? -1 : (int)bytes_written;
}

int filesystem_seek_file(
    unsigned int owner, unsigned int handle, int offset, int whence)
{
    Ext2FileStream *stream;
    unsigned int base;
    unsigned int position;
    unsigned int magnitude;

    if (handle == 0 || handle > EXT2_FILE_STREAM_LIMIT)
    {
        return -1;
    }
    stream = &file_streams[handle - 1];
    if (!stream->active || stream->owner != owner)
    {
        return -1;
    }
    if (whence == 0) base = 0;
    else if (whence == 1) base = stream->position;
    else if (whence == 2) base = stream->inode.size;
    else return -1;
    if (base > 0x7FFFFFFF) return -1;
    if (offset < 0)
    {
        magnitude = 0U - (unsigned int)offset;
        if (magnitude > base) return -1;
        position = base - magnitude;
    }
    else
    {
        if ((unsigned int)offset > 0x7FFFFFFF - base) return -1;
        position = base + (unsigned int)offset;
    }
    if (position > stream->inode.size) return -1;
    stream->position = position;
    return (int)position;
}

int filesystem_truncate_file(unsigned int owner, unsigned int handle)
{
    Ext2FileStream *stream;
    unsigned int previous_size;

    if (handle == 0 || handle > EXT2_FILE_STREAM_LIMIT)
    {
        return -1;
    }
    stream = &file_streams[handle - 1];
    if (!stream->active || stream->owner != owner ||
        stream->filesystem.drive != 1)
    {
        return -1;
    }

    previous_size = stream->inode.size;
    stream->inode.size = 0;
    if (!ext2_write_inode(&stream->filesystem, &stream->inode))
    {
        stream->inode.size = previous_size;
        return -1;
    }
    stream->position = 0;
    return 0;
}

int filesystem_read_file(
    unsigned int owner,
    unsigned int handle,
    unsigned int *buffer,
    unsigned int capacity)
{
    Ext2FileStream *stream;
    unsigned int bytes_to_read;
    unsigned int position;
    unsigned int bytes_read = 0;

    if (handle == 0 || handle > EXT2_FILE_STREAM_LIMIT ||
        buffer == 0 || capacity == 0 || capacity > 256)
    {
        return -1;
    }
    stream = &file_streams[handle - 1];
    if (!stream->active || stream->owner != owner)
    {
        return -1;
    }
    if (stream->position >= stream->inode.size)
    {
        return 0;
    }

    bytes_to_read = stream->inode.size - stream->position;
    if (bytes_to_read > capacity)
    {
        bytes_to_read = capacity;
    }
    position = stream->position;
    while (bytes_read < bytes_to_read)
    {
        unsigned char block[EXT2_BLOCK_SIZE];
        unsigned int logical_block = position / EXT2_BLOCK_SIZE;
        unsigned int block_offset = position % EXT2_BLOCK_SIZE;
        unsigned int chunk_size = EXT2_BLOCK_SIZE - block_offset;
        unsigned int physical_block;
        unsigned int index;

        if (chunk_size > bytes_to_read - bytes_read)
        {
            chunk_size = bytes_to_read - bytes_read;
        }
        if (!ext2_get_inode_block(
            &stream->filesystem,
            &stream->inode,
            logical_block,
            &physical_block))
        {
            return -1;
        }
        if (physical_block == 0)
        {
            for (index = 0; index < chunk_size; ++index)
            {
                buffer[bytes_read + index] = 0;
            }
        }
        else
        {
            if (!ext2_read_block(
                &stream->filesystem, physical_block, block))
            {
                return -1;
            }
            for (index = 0; index < chunk_size; ++index)
            {
                buffer[bytes_read + index] =
                    block[block_offset + index];
            }
        }
        bytes_read += chunk_size;
        position += chunk_size;
    }

    stream->position = position;
    return (int)bytes_read;
}

int filesystem_close_file(unsigned int owner, unsigned int handle)
{
    Ext2FileStream *stream;

    if (handle == 0 || handle > EXT2_FILE_STREAM_LIMIT)
    {
        return -1;
    }
    stream = &file_streams[handle - 1];
    if (!stream->active || stream->owner != owner)
    {
        return -1;
    }
    stream->active = 0;
    return 0;
}

void filesystem_close_process_files(unsigned int owner)
{
    unsigned int index;

    for (index = 0; index < EXT2_FILE_STREAM_LIMIT; ++index)
    {
        if (file_streams[index].active &&
            file_streams[index].owner == owner)
        {
            file_streams[index].active = 0;
        }
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
    char executable_path[FILESYSTEM_PATH_MAX + 1];
    char canonical_path[FILESYSTEM_PATH_MAX + 1];
    char normalized_path[FILESYSTEM_PATH_MAX + 1];
    unsigned int drive;
    unsigned int name_length = 0;
    unsigned int path_length = 0;
    unsigned int component_start = 0;
    unsigned int index;
    int has_extension = 0;
    unsigned int high_size;
    unsigned int block_index;
    int lookup_result;
    enum FileSystemError mount_result;

    if (buffer == 0 || file_size == 0 ||
        name == 0 || name[0] == '\0')
    {
        return FILE_READ_INVALID_NAME;
    }

    while (name[name_length] != '\0')
    {
        if (name_length == FILESYSTEM_PATH_MAX)
        {
            return FILE_READ_INVALID_NAME;
        }
        ++name_length;
    }

    if (name[1] != ':' && name[0] != '/' &&
        name[0] >= ' ' && name[0] <= '~')
    {
        int has_separator = 0;

        for (index = 0; index < name_length; ++index)
        {
            if (name[index] == '/')
            {
                has_separator = 1;
            }
        }
        if (!has_separator)
        {
            static const char prefix[] = "A:/";

            if (name_length + sizeof(prefix) - 1 > FILESYSTEM_PATH_MAX)
            {
                return FILE_READ_INVALID_NAME;
            }
            executable_path[0] = prefix[0];
            executable_path[1] = prefix[1];
            executable_path[2] = prefix[2];
            for (index = 0; index <= name_length; ++index)
            {
                executable_path[index + 3] = name[index];
            }
            name = executable_path;
        }
    }

    if (!filesystem_prepare_path(
        name, canonical_path, normalized_path, &drive))
    {
        return FILE_READ_INVALID_NAME;
    }

    while (normalized_path[path_length] != '\0')
    {
        if (normalized_path[path_length] == '/')
        {
            component_start = path_length + 1;
        }
        ++path_length;
    }
    if (path_length == 1 || component_start == path_length)
    {
        return FILE_READ_INVALID_NAME;
    }
    for (index = component_start; index < path_length; ++index)
    {
        if (normalized_path[index] == '.')
        {
            has_extension = 1;
        }
    }
    if (!has_extension)
    {
        static const char extension[] = ".EXE";

        if (path_length + sizeof(extension) - 1 > FILESYSTEM_PATH_MAX)
        {
            return FILE_READ_INVALID_NAME;
        }
        for (index = 0; index < sizeof(extension); ++index)
        {
            normalized_path[path_length + index] = extension[index];
        }
    }

    mount_result = filesystem_mount_drive(&filesystem, drive);
    if (mount_result == FILESYSTEM_DISK_READ_ERROR)
    {
        return FILE_READ_DISK_ERROR;
    }
    if (mount_result != FILESYSTEM_OK)
    {
        return FILE_READ_CORRUPT;
    }

    lookup_result = ext2_lookup_file_path(
        &filesystem, normalized_path, &inode);
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
