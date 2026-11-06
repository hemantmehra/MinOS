#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"

int process_print_string(
    Process *process,
    unsigned int address)
{
    const unsigned int end = process->code_base + process->code_size;
    const char *text;
    unsigned int index;
    unsigned int length;

    if (address < process->code_base || address >= end)
    {
        return 0;
    }

    text = (const char *)address;
    for (index = address; index < end; ++index)
    {
        if (*(const char *)index == '\0')
        {
            length = index - address;
            if (!process_stream_write(
                process,
                process->standard_output_handle,
                (const unsigned char *)text,
                length))
            {
                terminal_write(
                    "process: pipe buffer full or reader closed; terminating.\n");
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

int process_stack_buffer_is_valid(
    const Process *process,
    unsigned int address,
    unsigned int byte_count)
{
    unsigned int stack_start =
        process->code_base + PROCESS_SLOT_SIZE -
        PROCESS_STACK_PAGE_COUNT * 4096;
    unsigned int stack_end = process->code_base + PROCESS_SLOT_SIZE;

    return byte_count != 0 &&
        address >= stack_start &&
        address < stack_end &&
        byte_count <= stack_end - address;
}

int process_copy_user_path(
    const Process *process,
    unsigned int address,
    char path[128])
{
    unsigned int image_end = process->code_base + process->code_size;
    unsigned int index;

    if (address >= process->code_base && address < image_end)
    {
        for (index = 0; index < 128 && address + index < image_end; ++index)
        {
            path[index] = *(const char *)(address + index);
            if (path[index] == '\0')
            {
                return 1;
            }
        }
        return 0;
    }

    if (!process_stack_buffer_is_valid(process, address, 128 * 4))
    {
        return 0;
    }
    for (index = 0; index < 128; ++index)
    {
        unsigned int character = ((const unsigned int *)address)[index];

        if (character > 255)
        {
            return 0;
        }
        path[index] = (char)character;
        if (character == 0)
        {
            return 1;
        }
    }
    return 0;
}

int process_copy_user_title(
    const Process *process,
    unsigned int address,
    char title[32])
{
    unsigned int image_end = process->code_base + process->code_size;
    unsigned int index;

    if (address < process->code_base || address >= image_end)
    {
        return 0;
    }

    for (index = 0; index < 32 && address + index < image_end; ++index)
    {
        title[index] = *(const char *)(address + index);
        if (title[index] == '\0')
        {
            return index != 0;
        }
    }
    return 0;
}

int process_copy_string_to_array(
    const char *source,
    unsigned int *destination,
    unsigned int capacity)
{
    unsigned int length = 0;

    while (length < capacity && source[length] != '\0')
    {
        destination[length] = (unsigned char)source[length];
        ++length;
    }
    if (length == capacity)
    {
        return -1;
    }
    destination[length] = 0;
    return (int)length;
}
