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
    unsigned int root_sector;
    unsigned int root_sector_count;
    unsigned int root_entry_count;
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
        terminal_write("Commands: echo <text>, hostname, ls, clear, help\n");
    }
    else if (command_equals(command, "clear"))
    {
        terminal_clear();
    }
    else if (command_equals(command, "ls"))
    {
        filesystem_list_root();
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
