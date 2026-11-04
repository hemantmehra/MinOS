#include <intrin.h>
#include "kernel.h"
#include "kernel_log.h"

#define PAGE_SIZE 4096
#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES 1024
#define PHYSICAL_MEMORY_SIZE (16 * 1024 * 1024)
#define MANAGED_PAGE_COUNT (PHYSICAL_MEMORY_SIZE / PAGE_SIZE)
#define RESERVED_PAGE_COUNT (1024 * 1024 / PAGE_SIZE)
#define PAGE_BITMAP_SIZE (MANAGED_PAGE_COUNT / 8)
#define PAGE_PRESENT 0x001
#define PAGE_WRITABLE 0x002
#define PAGE_USER 0x004
#define PAGE_ALLOWED_FLAGS (PAGE_WRITABLE | PAGE_USER)

__declspec(align(4096))
static unsigned int page_directory[PAGE_DIRECTORY_ENTRIES];
__declspec(align(4096))
static unsigned int identity_page_tables[4][PAGE_TABLE_ENTRIES];
static unsigned char allocated_pages[PAGE_BITMAP_SIZE];

static int page_is_allocated(unsigned int page)
{
    return (allocated_pages[page >> 3] & (1u << (page & 7))) != 0;
}

static void mark_page_allocated(unsigned int page)
{
    allocated_pages[page >> 3] |= (unsigned char)(1u << (page & 7));
}

static void mark_page_free(unsigned int page)
{
    allocated_pages[page >> 3] &=
        (unsigned char)~(1u << (page & 7));
}

void memory_initialize(void)
{
    volatile unsigned int *directory = page_directory;
    volatile unsigned char *page_bitmap = allocated_pages;
    unsigned int directory_index;
    unsigned int table_index;
    unsigned int page;

    for (directory_index = 0;
        directory_index < PAGE_DIRECTORY_ENTRIES;
        ++directory_index)
    {
        directory[directory_index] = 0;
    }

    for (directory_index = 0; directory_index < 4; ++directory_index)
    {
        volatile unsigned int *page_table =
            identity_page_tables[directory_index];

        for (table_index = 0;
            table_index < PAGE_TABLE_ENTRIES;
            ++table_index)
        {
            page = directory_index * PAGE_TABLE_ENTRIES + table_index;
            page_table[table_index] =
                page * PAGE_SIZE | PAGE_PRESENT | PAGE_WRITABLE;
        }

        page_directory[directory_index] =
            (unsigned int)identity_page_tables[directory_index] |
            PAGE_PRESENT | PAGE_WRITABLE;
    }

    for (page = 0; page < PAGE_BITMAP_SIZE; ++page)
    {
        page_bitmap[page] = 0;
    }
    for (page = 0; page < RESERVED_PAGE_COUNT; ++page)
    {
        mark_page_allocated(page);
    }

    __asm
    {
        mov eax, offset page_directory
        mov cr3, eax
        mov eax, cr0
        or eax, 080000000h
        mov cr0, eax
    }
    kernel_log(KERNEL_LOG_INFO, "memory", "paging initialized");
}

unsigned int memory_allocate_page(void)
{
    unsigned int page;

    for (page = RESERVED_PAGE_COUNT; page < MANAGED_PAGE_COUNT; ++page)
    {
        if (!page_is_allocated(page))
        {
            mark_page_allocated(page);
            return page * PAGE_SIZE;
        }
    }

    kernel_log(KERNEL_LOG_ERROR, "memory", "physical pages exhausted");
    return 0;
}

int memory_free_page(unsigned int physical_address)
{
    unsigned int page;

    if ((physical_address & (PAGE_SIZE - 1)) != 0 ||
        physical_address < RESERVED_PAGE_COUNT * PAGE_SIZE ||
        physical_address >= PHYSICAL_MEMORY_SIZE)
    {
        return 0;
    }

    page = physical_address / PAGE_SIZE;
    if (!page_is_allocated(page))
    {
        return 0;
    }

    mark_page_free(page);
    return 1;
}

int memory_map_page(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags)
{
    unsigned int directory_index = virtual_address >> 22;
    unsigned int table_index = (virtual_address >> 12) & 0x3FF;
    unsigned int *page_table;
    unsigned int table_address;

    if ((virtual_address & (PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < PHYSICAL_MEMORY_SIZE ||
        (flags & ~PAGE_ALLOWED_FLAGS) != 0)
    {
        return 0;
    }

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0)
    {
        table_address = memory_allocate_page();
        if (table_address == 0)
        {
            return 0;
        }

        page_table = (unsigned int *)table_address;
        for (table_index = 0; table_index < PAGE_TABLE_ENTRIES; ++table_index)
        {
            page_table[table_index] = 0;
        }
        page_directory[directory_index] =
            table_address | PAGE_PRESENT | PAGE_WRITABLE;
        table_index = (virtual_address >> 12) & 0x3FF;
    }
    else
    {
        page_table = (unsigned int *)(
            page_directory[directory_index] & ~(PAGE_SIZE - 1));
    }

    if ((page_table[table_index] & PAGE_PRESENT) != 0)
    {
        return 0;
    }

    page_table[table_index] =
        physical_address | PAGE_PRESENT | flags;
    if ((flags & PAGE_USER) != 0)
    {
        page_directory[directory_index] |= PAGE_USER;
    }

    __invlpg((void *)virtual_address);
    return 1;
}

int memory_unmap_page(
    unsigned int virtual_address,
    unsigned int *physical_address)
{
    unsigned int directory_index = virtual_address >> 22;
    unsigned int table_index = (virtual_address >> 12) & 0x3FF;
    unsigned int *page_table;

    if ((virtual_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < PHYSICAL_MEMORY_SIZE ||
        (page_directory[directory_index] & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    page_table = (unsigned int *)(
        page_directory[directory_index] & ~(PAGE_SIZE - 1));
    if ((page_table[table_index] & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    if (physical_address != 0)
    {
        *physical_address = page_table[table_index] & ~(PAGE_SIZE - 1);
    }
    page_table[table_index] = 0;
    __invlpg((void *)virtual_address);
    return 1;
}

int memory_translate(
    unsigned int virtual_address,
    unsigned int *physical_address)
{
    unsigned int directory_index = virtual_address >> 22;
    unsigned int table_index = (virtual_address >> 12) & 0x3FF;
    unsigned int *page_table;
    unsigned int entry;

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    page_table = (unsigned int *)(
        page_directory[directory_index] & ~(PAGE_SIZE - 1));
    entry = page_table[table_index];
    if ((entry & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    *physical_address =
        (entry & ~(PAGE_SIZE - 1)) | (virtual_address & (PAGE_SIZE - 1));
    return 1;
}

unsigned int memory_available_pages(void)
{
    unsigned int page;
    unsigned int available = 0;

    for (page = RESERVED_PAGE_COUNT; page < MANAGED_PAGE_COUNT; ++page)
    {
        if (!page_is_allocated(page))
        {
            ++available;
        }
    }
    return available;
}
