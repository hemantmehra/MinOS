#include <intrin.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define COMMAND_BUFFER_SIZE 128
#define FILESYSTEM_START_SECTOR 33
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

static unsigned short vga_cursor_position(void)
{
    unsigned short position;

    __outbyte(0x3D4, 14);
    position = (unsigned short)__inbyte(0x3D5) << 8;
    __outbyte(0x3D4, 15);
    position |= __inbyte(0x3D5);
    return position;
}

static void vga_set_cursor_position(unsigned short position)
{
    __outbyte(0x3D4, 14);
    __outbyte(0x3D5, (unsigned char)(position >> 8));
    __outbyte(0x3D4, 15);
    __outbyte(0x3D5, (unsigned char)position);
}

static void vga_scroll(void)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * (VGA_HEIGHT - 1); ++index)
    {
        video_memory[index] = video_memory[index + VGA_WIDTH];
    }

    for (; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        video_memory[index] = 0x0F20;
    }
}

static void terminal_write_character(char character)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned short position = vga_cursor_position();

    if (character == '\r')
    {
        position -= position % VGA_WIDTH;
    }
    else if (character == '\n')
    {
        position += VGA_WIDTH - position % VGA_WIDTH;
    }
    else if (character == '\b')
    {
        if (position != 0)
        {
            --position;
            video_memory[position] = 0x0F20;
        }
    }
    else
    {
        video_memory[position] = (unsigned short)(0x0F00 | (unsigned char)character);
        ++position;
        __outbyte(0xE9, (unsigned char)character);
    }

    if (position >= VGA_WIDTH * VGA_HEIGHT)
    {
        vga_scroll();
        position -= VGA_WIDTH;
    }

    vga_set_cursor_position(position);

    if (character == '\n')
    {
        __outbyte(0xE9, '\n');
    }
    else if (character == '\b')
    {
        __outbyte(0xE9, '\b');
    }
}

static void terminal_write(const char *text)
{
    while (*text != '\0')
    {
        terminal_write_character(*text);
        ++text;
    }
}

static void terminal_clear(void)
{
    volatile unsigned short *video_memory =
        (volatile unsigned short *)0xB8000;
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        video_memory[index] = 0x0F20;
    }

    vga_set_cursor_position(0);
}

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

static void terminal_write_unsigned(unsigned int value)
{
    char digits[10];
    unsigned int length = 0;

    do
    {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (length != 0)
    {
        terminal_write_character(digits[--length]);
    }
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

static void filesystem_list_root(void)
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

enum CreateDirectoryResult
{
    CREATE_DIRECTORY_OK,
    CREATE_DIRECTORY_INVALID_NAME,
    CREATE_DIRECTORY_EXISTS,
    CREATE_DIRECTORY_ROOT_FULL,
    CREATE_DIRECTORY_DISK_FULL,
    CREATE_DIRECTORY_IO_ERROR,
    CREATE_DIRECTORY_FILESYSTEM_ERROR
};

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

static enum CreateDirectoryResult filesystem_create_root_directory(
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

static char translate_scancode(
    unsigned char scancode,
    unsigned char shift_pressed,
    unsigned char caps_lock)
{
    char character = '\0';

    if (scancode >= 0x02 && scancode <= 0x0B)
    {
        const char *digits = shift_pressed ? "!@#$%^&*()" : "1234567890";
        character = digits[scancode - 0x02];
    }
    else if (scancode >= 0x10 && scancode <= 0x19)
    {
        character = "qwertyuiop"[scancode - 0x10];
    }
    else if (scancode >= 0x1E && scancode <= 0x26)
    {
        character = "asdfghjkl"[scancode - 0x1E];
    }
    else if (scancode >= 0x2C && scancode <= 0x32)
    {
        character = "zxcvbnm"[scancode - 0x2C];
    }
    else if (scancode == 0x39)
    {
        character = ' ';
    }
    else
    {
        switch (scancode)
        {
        case 0x0C: character = shift_pressed ? '_' : '-'; break;
        case 0x0D: character = shift_pressed ? '+' : '='; break;
        case 0x1A: character = shift_pressed ? '{' : '['; break;
        case 0x1B: character = shift_pressed ? '}' : ']'; break;
        case 0x27: character = shift_pressed ? ':' : ';'; break;
        case 0x28: character = shift_pressed ? '"' : '\''; break;
        case 0x29: character = shift_pressed ? '~' : '`'; break;
        case 0x2B: character = shift_pressed ? '|' : '\\'; break;
        case 0x33: character = shift_pressed ? '<' : ','; break;
        case 0x34: character = shift_pressed ? '>' : '.'; break;
        case 0x35: character = shift_pressed ? '?' : '/'; break;
        default: break;
        }
    }

    if (character >= 'a' && character <= 'z' &&
        (shift_pressed != caps_lock))
    {
        character = (char)(character - 'a' + 'A');
    }

    return character;
}

static char keyboard_read_character(
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended)
{
    for (;;)
    {
        unsigned char scancode;
        unsigned char status = __inbyte(0x64);

        if ((status & 0x01) == 0)
        {
            continue;
        }

        scancode = __inbyte(0x60);

        if (*extended)
        {
            *extended = 0;
            continue;
        }

        if (scancode == 0xE0)
        {
            *extended = 1;
            continue;
        }

        if (scancode == 0x2A || scancode == 0x36)
        {
            *shift_pressed = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6)
        {
            *shift_pressed = 0;
            continue;
        }

        if ((scancode & 0x80) != 0)
        {
            continue;
        }

        if (scancode == 0x3A)
        {
            *caps_lock = (unsigned char)!*caps_lock;
            continue;
        }

        if (scancode == 0x1C)
        {
            return '\n';
        }

        if (scancode == 0x0E)
        {
            return '\b';
        }

        return translate_scancode(scancode, *shift_pressed, *caps_lock);
    }
}

static int command_equals(const char *command, const char *expected)
{
    while (*command != '\0' && *command == *expected)
    {
        ++command;
        ++expected;
    }

    return *command == '\0' && *expected == '\0';
}

static int command_token_equals(
    const char *command,
    const char *end,
    const char *expected)
{
    while (command != end && *command == *expected)
    {
        ++command;
        ++expected;
    }

    return command == end && *expected == '\0';
}

static int is_space(char character)
{
    return character == ' ' || character == '\t';
}

static void run_command(char *command)
{
    char *argument;

    while (is_space(*command))
    {
        ++command;
    }

    argument = command;
    while (*argument != '\0' && !is_space(*argument))
    {
        ++argument;
    }

    if (argument - command == 4 &&
        command[0] == 'e' && command[1] == 'c' &&
        command[2] == 'h' && command[3] == 'o')
    {
        while (is_space(*argument))
        {
            ++argument;
        }
        terminal_write(argument);
        terminal_write("\n");
    }
    else if (command_equals(command, "hostname"))
    {
        terminal_write("minios\n");
    }
    else if (command_equals(command, "help"))
    {
        terminal_write(
            "Commands: echo <text>, hostname, ls, mkdir, clear, help\n");
    }
    else if (command_equals(command, "clear"))
    {
        terminal_clear();
    }
    else if (command_equals(command, "ls"))
    {
        filesystem_list_root();
    }
    else if (command_token_equals(command, argument, "mkdir"))
    {
        char *name;
        char *name_end;
        enum CreateDirectoryResult result;

        while (is_space(*argument))
        {
            ++argument;
        }

        if (*argument == '\0')
        {
            terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
            return;
        }

        name = argument;
        while (*argument != '\0' && !is_space(*argument))
        {
            ++argument;
        }
        name_end = argument;

        while (is_space(*argument))
        {
            ++argument;
        }

        if (*argument != '\0')
        {
            terminal_write("Usage: mkdir <name> (1-8 letters, digits, _ or -)\n");
            return;
        }

        *name_end = '\0';
        result = filesystem_create_root_directory(name);
        switch (result)
        {
        case CREATE_DIRECTORY_OK:
            terminal_write("Created directory ");
            terminal_write(name);
            terminal_write("\n");
            break;
        case CREATE_DIRECTORY_INVALID_NAME:
            terminal_write("mkdir: name must be 1-8 letters, digits, _ or -.\n");
            break;
        case CREATE_DIRECTORY_EXISTS:
            terminal_write("mkdir: directory already exists.\n");
            break;
        case CREATE_DIRECTORY_ROOT_FULL:
            terminal_write("mkdir: current directory is full.\n");
            break;
        case CREATE_DIRECTORY_DISK_FULL:
            terminal_write("mkdir: filesystem is full.\n");
            break;
        case CREATE_DIRECTORY_IO_ERROR:
            terminal_write("mkdir: disk read or write failed.\n");
            break;
        default:
            terminal_write("mkdir: could not mount the FAT16 filesystem.\n");
            break;
        }
    }
    else
    {
        terminal_write("Unknown command: ");
        terminal_write(command);
        terminal_write("\n");
    }
}

void __cdecl kernel_main(void)
{
    char command[COMMAND_BUFFER_SIZE];
    unsigned char shift_pressed = 0;
    unsigned char caps_lock = 0;
    unsigned char extended = 0;

    terminal_clear();
    terminal_write("Hello, world!\n");
    terminal_write("Type help for available commands.\n");

    for (;;)
    {
        unsigned int length = 0;
        char character;

        terminal_write("minios> ");

        for (;;)
        {
            character = keyboard_read_character(
                &shift_pressed, &caps_lock, &extended);

            if (character == '\n')
            {
                terminal_write("\n");
                break;
            }

            if (character == '\b')
            {
                if (length != 0)
                {
                    --length;
                    terminal_write_character('\b');
                }
                continue;
            }

            if (character >= ' ' && character <= '~' &&
                length < COMMAND_BUFFER_SIZE - 1)
            {
                command[length++] = character;
                terminal_write_character(character);
            }
        }

        command[length] = '\0';
        run_command(command);
    }
}
