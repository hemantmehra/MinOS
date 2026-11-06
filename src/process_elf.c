#include "kernel_log.h"
#include "process_internal.h"
#include "elf_format.h"

#define ELF_SCRATCH_BASE (PROCESS_BASE + 0x00100000)

#if ELF_IMAGE_BASE != PROCESS_BASE || ELF_IMAGE_LIMIT != EXECUTABLE_MAX_SIZE
#error ELF and process image limits must match
#endif

/* Called in the new address space with interrupts disabled. The original
   ELF file must be staged away from the destination before loading segments. */
int process_load_elf(unsigned int file_size, unsigned int *entry_offset,
    unsigned int *image_size)
{
    unsigned int pages = (file_size + 4095) / 4096;
    unsigned int mapped = 0;
    unsigned int index;
    unsigned int phoff;
    unsigned int phnum;
    unsigned char *image = (unsigned char *)PROCESS_BASE;
    unsigned char *staging = (unsigned char *)ELF_SCRATCH_BASE;
    int result = -1;

    while (mapped < pages)
    {
        unsigned int physical = memory_allocate_page();

        if (physical == 0)
        {
            goto cleanup;
        }
        if (!memory_map_page_in(
            memory_current_address_space(),
            ELF_SCRATCH_BASE + mapped * 4096, physical, 0x02))
        {
            if (!memory_free_page(physical))
            {
                kernel_log(
                    KERNEL_LOG_ERROR, "process", "ELF staging page release failed");
            }
            goto cleanup;
        }
        ++mapped;
    }
    for (index = 0; index < file_size; ++index)
    {
        staging[index] = image[index];
    }
    if (!elf_validate(staging, file_size, entry_offset, image_size))
    {
        result = 0;
        goto cleanup;
    }
    for (index = 0; index < *image_size; ++index)
    {
        ((volatile unsigned char *)image)[index] = 0;
    }
    phoff = read_little_endian_dword(staging + 28);
    phnum = (unsigned int)staging[44] | ((unsigned int)staging[45] << 8);
    for (index = 0; index < phnum; ++index)
    {
        const unsigned char *header =
            staging + phoff + index * ELF_PROGRAM_HEADER_SIZE;
        unsigned int offset;
        unsigned int address;
        unsigned int length;
        unsigned int byte;

        if (read_little_endian_dword(header) != ELF_LOAD)
        {
            continue;
        }
        offset = read_little_endian_dword(header + 4);
        address = read_little_endian_dword(header + 8);
        length = read_little_endian_dword(header + 16);
        for (byte = 0; byte < length; ++byte)
        {
            ((volatile unsigned char *)address)[byte] = staging[offset + byte];
        }
    }
    result = 1;

cleanup:
    while (mapped != 0)
    {
        unsigned int physical;
        --mapped;
        if (!memory_unmap_page_in(
                memory_current_address_space(),
                ELF_SCRATCH_BASE + mapped * 4096, &physical) ||
            !memory_free_page(physical))
        {
            kernel_log(KERNEL_LOG_ERROR, "process", "ELF staging cleanup failed");
            return -1;
        }
    }
    return result;
}
