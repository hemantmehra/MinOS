#include "elf_format.h"

static unsigned int elf_u32(const unsigned char *data)
{
    return (unsigned int)data[0] | ((unsigned int)data[1] << 8) |
        ((unsigned int)data[2] << 16) | ((unsigned int)data[3] << 24);
}

static unsigned int elf_u16(const unsigned char *data)
{
    return (unsigned int)data[0] | ((unsigned int)data[1] << 8);
}

int elf_validate(
    const unsigned char *data,
    unsigned int size,
    unsigned int *entry_offset,
    unsigned int *image_size)
{
    unsigned int phoff;
    unsigned int phnum;
    unsigned int entry;
    unsigned int max_end = 0;
    unsigned int index;
    int entry_valid = 0;

    if (size < ELF_HEADER_SIZE ||
        data[0] != 0x7F || data[1] != 'E' ||
        data[2] != 'L' || data[3] != 'F' ||
        data[4] != 1 || data[5] != 1 || data[6] != 1 ||
        elf_u16(data + 16) != 2 || elf_u16(data + 18) != 3 ||
        elf_u32(data + 20) != 1 ||
        elf_u16(data + 40) != ELF_HEADER_SIZE ||
        elf_u16(data + 42) != ELF_PROGRAM_HEADER_SIZE)
    {
        return 0;
    }
    phoff = elf_u32(data + 28);
    phnum = elf_u16(data + 44);
    entry = elf_u32(data + 24);
    if (phnum == 0 || phnum > ELF_MAX_PROGRAM_HEADERS ||
        phoff > size || phnum > (size - phoff) / ELF_PROGRAM_HEADER_SIZE)
    {
        return 0;
    }
    for (index = 0; index < phnum; ++index)
    {
        const unsigned char *header =
            data + phoff + index * ELF_PROGRAM_HEADER_SIZE;
        unsigned int type = elf_u32(header);
        unsigned int offset;
        unsigned int address;
        unsigned int filesz;
        unsigned int memsz;
        unsigned int align;
        unsigned int previous;

        if (type == ELF_DYNAMIC || type == ELF_INTERP || type == ELF_TLS)
        {
            return 0;
        }
        if (type != ELF_LOAD)
        {
            continue;
        }
        offset = elf_u32(header + 4);
        address = elf_u32(header + 8);
        filesz = elf_u32(header + 16);
        memsz = elf_u32(header + 20);
        align = elf_u32(header + 28);
        if (filesz > memsz || offset > size || filesz > size - offset ||
            address < ELF_IMAGE_BASE ||
            address - ELF_IMAGE_BASE > ELF_IMAGE_LIMIT ||
            memsz > ELF_IMAGE_LIMIT - (address - ELF_IMAGE_BASE) ||
            (align > 1 && ((align & (align - 1)) != 0 ||
                (offset & (align - 1)) != (address & (align - 1)))))
        {
            return 0;
        }
        for (previous = 0; previous < index; ++previous)
        {
            const unsigned char *other =
                data + phoff + previous * ELF_PROGRAM_HEADER_SIZE;
            unsigned int other_address;
            unsigned int other_size;

            if (elf_u32(other) != ELF_LOAD)
            {
                continue;
            }
            other_address = elf_u32(other + 8);
            other_size = elf_u32(other + 20);
            if (memsz != 0 && other_size != 0 &&
                address < other_address + other_size &&
                other_address < address + memsz)
            {
                return 0;
            }
        }
        if ((elf_u32(header + 24) & ELF_EXECUTABLE) &&
            entry >= address && entry - address < filesz)
        {
            entry_valid = 1;
        }
        if (address - ELF_IMAGE_BASE + memsz > max_end)
        {
            max_end = address - ELF_IMAGE_BASE + memsz;
        }
    }
    if (!entry_valid || max_end == 0)
    {
        return 0;
    }
    *entry_offset = entry - ELF_IMAGE_BASE;
    *image_size = max_end;
    return 1;
}
