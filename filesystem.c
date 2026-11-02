#include <intrin.h>
#include "kernel.h"

#define FILESYSTEM_START_SECTOR 65
#define DISK_SECTOR_COUNT 32768
#define SECTOR_SIZE 512
#define ATA_DATA_PORT 0x1F0
#define ATA_ERROR_PORT 0x1F1
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
    unsigned int volume_end_sector;
    unsigned int fat_start_sector;
    unsigned int sectors_per_fat;
    unsigned int data_start_sector;
    unsigned int root_sector;
    unsigned int root_sector_count;
    unsigned int root_entry_count;
    unsigned int cluster_count;
    unsigned char fat_count;
    unsigned char sectors_per_cluster;
} FileSystem;

enum FileSystemError
{
    FILESYSTEM_OK,
    FILESYSTEM_DISK_READ_ERROR,
    FILESYSTEM_INVALID_BOOT_SECTOR,
    FILESYSTEM_INVALID_BPB,
    FILESYSTEM_NOT_FAT16
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

static enum FileSystemError filesystem_mount(
    FileSystem *filesystem,
    unsigned char *sector)
{
    unsigned int total_sectors;
    unsigned int reserved_sectors;
    unsigned int fat_count;
    unsigned int sectors_per_fat;
    unsigned int root_sectors;
    unsigned int first_root_sector;
    unsigned int first_data_sector;
    unsigned int cluster_count;
    unsigned int volume_end;
    unsigned short bytes_per_sector;
    unsigned short root_entries;
    unsigned char sectors_per_cluster;

    if (!ata_read_sector(FILESYSTEM_START_SECTOR, sector))
    {
        return FILESYSTEM_DISK_READ_ERROR;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA)
    {
        return FILESYSTEM_INVALID_BOOT_SECTOR;
    }

    bytes_per_sector = read_little_endian_word(&sector[11]);
    sectors_per_cluster = sector[13];
    reserved_sectors = read_little_endian_word(&sector[14]);
    fat_count = sector[16];
    root_entries = read_little_endian_word(&sector[17]);
    total_sectors = read_little_endian_word(&sector[19]);
    if (total_sectors == 0)
    {
        total_sectors = read_little_endian_dword(&sector[32]);
    }
    sectors_per_fat = read_little_endian_word(&sector[22]);

    if (bytes_per_sector != SECTOR_SIZE ||
        sectors_per_cluster == 0 ||
        (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 ||
        reserved_sectors == 0 || fat_count == 0 || fat_count > 2 ||
        root_entries == 0 || total_sectors == 0 || sectors_per_fat == 0)
    {
        return FILESYSTEM_INVALID_BPB;
    }

    root_sectors =
        ((unsigned int)root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    first_root_sector =
        FILESYSTEM_START_SECTOR + reserved_sectors + fat_count * sectors_per_fat;
    first_data_sector = first_root_sector + root_sectors;
    volume_end = FILESYSTEM_START_SECTOR + total_sectors;
    if (first_data_sector >= volume_end || volume_end > DISK_SECTOR_COUNT)
    {
        return FILESYSTEM_INVALID_BPB;
    }

    cluster_count =
        (volume_end - first_data_sector) / sectors_per_cluster;
    if (cluster_count < 4085 || cluster_count >= 65525)
    {
        return FILESYSTEM_NOT_FAT16;
    }

    if ((cluster_count + 2) * 2 > sectors_per_fat * SECTOR_SIZE)
    {
        return FILESYSTEM_INVALID_BPB;
    }

    filesystem->root_sector = first_root_sector;
    filesystem->root_sector_count = root_sectors;
    filesystem->root_entry_count = root_entries;
    filesystem->volume_end_sector = volume_end;
    filesystem->fat_start_sector = FILESYSTEM_START_SECTOR + reserved_sectors;
    filesystem->sectors_per_fat = sectors_per_fat;
    filesystem->data_start_sector = first_data_sector;
    filesystem->cluster_count = cluster_count;
    filesystem->fat_count = (unsigned char)fat_count;
    filesystem->sectors_per_cluster = sectors_per_cluster;
    return FILESYSTEM_OK;
}

static void terminal_write_fat_name(const unsigned char *entry)
{
    unsigned int base_length = 8;
    unsigned int extension_length = 3;
    unsigned int index;

    if (entry[0] == 0x05)
    {
        terminal_write_character((char)0xE5);
    }
    else
    {
        while (base_length != 0 && entry[base_length - 1] == ' ')
        {
            --base_length;
        }

        for (index = 0; index < base_length; ++index)
        {
            terminal_write_character((char)entry[index]);
        }
    }

    while (extension_length != 0 && entry[8 + extension_length - 1] == ' ')
    {
        --extension_length;
    }

    if (extension_length != 0)
    {
        terminal_write_character('.');
        for (index = 0; index < extension_length; ++index)
        {
            terminal_write_character((char)entry[8 + index]);
        }
    }
}

void filesystem_list_root(void)
{
    FileSystem filesystem;
    unsigned char sector[SECTOR_SIZE];
    unsigned int entries_seen = 0;
    unsigned int sector_index;
    enum FileSystemError error = filesystem_mount(&filesystem, sector);

    if (error == FILESYSTEM_DISK_READ_ERROR)
    {
        terminal_write("ls: could not read the filesystem from disk.\n");
        return;
    }
    if (error == FILESYSTEM_INVALID_BOOT_SECTOR)
    {
        terminal_write("ls: filesystem has an invalid boot-sector signature.\n");
        return;
    }
    if (error == FILESYSTEM_NOT_FAT16)
    {
        terminal_write("ls: volume does not have a FAT16 cluster count.\n");
        return;
    }
    if (error != FILESYSTEM_OK)
    {
        terminal_write("ls: filesystem has an invalid boot record.\n");
        return;
    }

    for (sector_index = 0;
        sector_index < filesystem.root_sector_count;
        ++sector_index)
    {
        unsigned int entry_index;

        if (!ata_read_sector(
            filesystem.root_sector + sector_index, sector))
        {
            terminal_write("ls: could not read the filesystem directory.\n");
            return;
        }

        for (entry_index = 0; entry_index < SECTOR_SIZE / 32; ++entry_index)
        {
            const unsigned char *entry = &sector[entry_index * 32];
            unsigned char attributes;
            unsigned int file_size;
            unsigned int index;

            if (entry[0] == 0)
            {
                if (entries_seen == 0)
                {
                    terminal_write("(empty)\n");
                }
                return;
            }

            if (entry[0] == 0xE5)
            {
                continue;
            }

            attributes = entry[11];
            if (attributes == 0x0F || (attributes & 0x08) != 0 ||
                entry[0] == '.')
            {
                continue;
            }

            if ((attributes & 0x10) != 0)
            {
                terminal_write("[DIR]  ");
                terminal_write_fat_name(entry);
                terminal_write("\n");
            }
            else
            {
                file_size = read_little_endian_dword(&entry[28]);
                terminal_write_fat_name(entry);
                for (index = 0; index < 2; ++index)
                {
                    terminal_write_character(' ');
                }
                terminal_write_unsigned(file_size);
                terminal_write(" bytes\n");
            }

            ++entries_seen;
        }
    }

    if (entries_seen == 0)
    {
        terminal_write("(empty)\n");
    }
}


static int filesystem_make_short_name(
    const char *name,
    unsigned char short_name[11])
{
    unsigned int index = 0;

    while (index < 11)
    {
        short_name[index] = ' ';
        ++index;
    }

    index = 0;
    while (name[index] != '\0')
    {
        char character = name[index];

        if (index == 8 ||
            !((character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '-'))
        {
            return 0;
        }

        if (character >= 'a' && character <= 'z')
        {
            character = (char)(character - 'a' + 'A');
        }

        short_name[index] = (unsigned char)character;
        ++index;
    }

    return index != 0;
}

static int filesystem_find_root_slot(
    const FileSystem *filesystem,
    const unsigned char short_name[11],
    unsigned int *slot_index,
    unsigned char *sector)
{
    unsigned int sector_index;
    unsigned int deleted_slot = filesystem->root_entry_count;

    for (sector_index = 0;
        sector_index < filesystem->root_sector_count;
        ++sector_index)
    {
        unsigned int entry_index;

        if (!ata_read_sector(filesystem->root_sector + sector_index, sector))
        {
            return -1;
        }

        for (entry_index = 0; entry_index < SECTOR_SIZE / 32; ++entry_index)
        {
            unsigned int index =
                sector_index * (SECTOR_SIZE / 32) + entry_index;
            unsigned char *entry = &sector[entry_index * 32];

            if (index >= filesystem->root_entry_count)
            {
                break;
            }

            if (entry[0] == 0)
            {
                *slot_index = deleted_slot == filesystem->root_entry_count
                    ? index
                    : deleted_slot;
                return 1;
            }

            if (entry[0] == 0xE5)
            {
                if (deleted_slot == filesystem->root_entry_count)
                {
                    deleted_slot = index;
                }
                continue;
            }

            if (entry[11] != 0x0F && (entry[11] & 0x08) == 0 &&
                (entry[11] & 0x10) != 0)
            {
                unsigned int name_index;

                for (name_index = 0; name_index < 11; ++name_index)
                {
                    if (entry[name_index] != short_name[name_index])
                    {
                        break;
                    }
                }

                if (name_index == 11)
                {
                    return 0;
                }
            }
        }
    }

    if (deleted_slot != filesystem->root_entry_count)
    {
        *slot_index = deleted_slot;
        return 1;
    }

    return 2;
}

static int filesystem_find_free_cluster(
    const FileSystem *filesystem,
    unsigned char *sector,
    unsigned short *free_cluster)
{
    unsigned int cluster;
    unsigned int cached_fat_sector = DISK_SECTOR_COUNT;

    for (cluster = 2; cluster < filesystem->cluster_count + 2; ++cluster)
    {
        unsigned int fat_offset = cluster * 2;
        unsigned int lba =
            filesystem->fat_start_sector + fat_offset / SECTOR_SIZE;
        unsigned int offset = fat_offset % SECTOR_SIZE;

        if (lba != cached_fat_sector)
        {
            if (!ata_read_sector(lba, sector))
            {
                return 0;
            }
            cached_fat_sector = lba;
        }

        if (read_little_endian_word(&sector[offset]) == 0)
        {
            *free_cluster = (unsigned short)cluster;
            return 1;
        }
    }

    return -1;
}

static int filesystem_mark_cluster_used(
    const FileSystem *filesystem,
    unsigned short cluster,
    unsigned char *sector)
{
    unsigned int fat_offset = (unsigned int)cluster * 2;
    unsigned int lba =
        filesystem->fat_start_sector + fat_offset / SECTOR_SIZE;
    unsigned int offset = fat_offset % SECTOR_SIZE;
    unsigned int fat_copy;

    if (!ata_read_sector(lba, sector))
    {
        return 0;
    }

    sector[offset] = 0xFF;
    sector[offset + 1] = 0xFF;

    for (fat_copy = 0; fat_copy < filesystem->fat_count; ++fat_copy)
    {
        unsigned int copy_lba =
            lba + fat_copy * filesystem->sectors_per_fat;

        if (fat_copy != 0 && !ata_read_sector(copy_lba, sector))
        {
            return 0;
        }

        sector[offset] = 0xFF;
        sector[offset + 1] = 0xFF;
        if (!ata_write_sector(copy_lba, sector))
        {
            return 0;
        }
    }

    return 1;
}

static int filesystem_initialize_directory(
    const FileSystem *filesystem,
    unsigned short cluster,
    unsigned char *sector)
{
    unsigned int sector_index;
    unsigned int cluster_start =
        filesystem->data_start_sector +
        ((unsigned int)cluster - 2) * filesystem->sectors_per_cluster;

    for (sector_index = 0;
        sector_index < filesystem->sectors_per_cluster;
        ++sector_index)
    {
        __stosw((unsigned short *)sector, 0, SECTOR_SIZE / 2);

        if (sector_index == 0)
        {
            unsigned int index;

            sector[0] = '.';
            for (index = 1; index < 11; ++index)
            {
                sector[index] = ' ';
            }
            sector[11] = 0x10;
            sector[16] = 0x21;
            sector[24] = 0x21;
            sector[26] = (unsigned char)cluster;
            sector[27] = (unsigned char)(cluster >> 8);

            sector[32] = '.';
            sector[33] = '.';
            for (index = 34; index < 43; ++index)
            {
                sector[index] = ' ';
            }
            sector[43] = 0x10;
            sector[48] = 0x21;
            sector[56] = 0x21;

            if (!ata_write_sector(cluster_start, sector))
            {
                return 0;
            }
        }
        else if (!ata_write_sector(cluster_start + sector_index, sector))
        {
            return 0;
        }
    }

    return 1;
}

enum CreateDirectoryResult filesystem_create_root_directory(
    const char *name)
{
    FileSystem filesystem;
    unsigned char sector[SECTOR_SIZE];
    unsigned char short_name[11];
    unsigned int slot_index;
    unsigned int slot_lba;
    unsigned int slot_offset;
    unsigned short cluster;
    int slot_result;
    int cluster_result;
    enum FileSystemError mount_result;

    if (!filesystem_make_short_name(name, short_name))
    {
        return CREATE_DIRECTORY_INVALID_NAME;
    }

    mount_result = filesystem_mount(&filesystem, sector);
    if (mount_result != FILESYSTEM_OK)
    {
        return CREATE_DIRECTORY_FILESYSTEM_ERROR;
    }

    slot_index = filesystem.root_entry_count;
    slot_result = filesystem_find_root_slot(
        &filesystem, short_name, &slot_index, sector);
    if (slot_result == -1)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (slot_result == 0)
    {
        return CREATE_DIRECTORY_EXISTS;
    }
    if (slot_result == 2)
    {
        return CREATE_DIRECTORY_ROOT_FULL;
    }

    cluster_result = filesystem_find_free_cluster(
        &filesystem, sector, &cluster);
    if (cluster_result == 0)
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }
    if (cluster_result < 0)
    {
        return CREATE_DIRECTORY_DISK_FULL;
    }

    if (!filesystem_mark_cluster_used(&filesystem, cluster, sector) ||
        !filesystem_initialize_directory(&filesystem, cluster, sector))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    slot_lba =
        filesystem.root_sector + slot_index / (SECTOR_SIZE / 32);
    slot_offset = (slot_index % (SECTOR_SIZE / 32)) * 32;
    if (!ata_read_sector(slot_lba, sector))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    {
        unsigned int index;
        unsigned char *entry = &sector[slot_offset];

        for (index = 0; index < 32; ++index)
        {
            entry[index] = 0;
        }

        for (index = 0; index < 11; ++index)
        {
            entry[index] = short_name[index];
        }
        entry[11] = 0x10;
        entry[16] = 0x21;
        entry[24] = 0x21;
        entry[26] = (unsigned char)cluster;
        entry[27] = (unsigned char)(cluster >> 8);
    }

    if (!ata_write_sector(slot_lba, sector))
    {
        return CREATE_DIRECTORY_IO_ERROR;
    }

    return CREATE_DIRECTORY_OK;
}
