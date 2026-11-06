#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"

void process_inherit_environment(Process *child, const Process *parent)
{
    unsigned int entry, character;
    for (entry = 0; entry < PROCESS_ENV_COUNT; ++entry)
    {
        for (character = 0; character < PROCESS_ENV_LENGTH; ++character)
        {
            child->environment[entry][character] =
                parent->environment[entry][character];
        }
    }
}

enum ProcessStartResult process_start(
    const char *filename,
    const char *arguments,
    unsigned int *process_id,
    int background)
{
    unsigned int flags = disable_interrupts();
    unsigned int slot;
    unsigned int index;
    unsigned int file_size = 0;
    unsigned int expected_size;
    unsigned int entry_offset;
    unsigned int pid;
    unsigned int stack_top;
    unsigned int previous_directory;
    unsigned int *stack;
    unsigned int argument_length = 0;
    unsigned int base_start = 0;
    enum FileReadResult read_result;
    Process *process = 0;

    if (arguments == 0)
    {
        arguments = "";
    }
    if (filename == 0)
    {
        restore_interrupts(flags);
        return PROCESS_START_INVALID_NAME;
    }
    for (index = 0; index < sizeof(processes[0].program_path) - 1 &&
        filename[index] != '\0'; ++index)
    {
    }
    if (filename[index] != '\0')
    {
        restore_interrupts(flags);
        return PROCESS_START_INVALID_NAME;
    }
    while (arguments[argument_length] != '\0')
    {
        if (argument_length == COMMAND_BUFFER_SIZE - 1)
        {
            restore_interrupts(flags);
            return PROCESS_START_ARGUMENT_TOO_LONG;
        }
        ++argument_length;
    }

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_UNUSED)
        {
            process = &processes[slot];
            break;
        }
    }
    if (process == 0)
    {
        if (!process_table_grow())
        {
            restore_interrupts(flags);
            return PROCESS_START_LIMIT_REACHED;
        }
        slot = process_count - 1;
        process = &processes[slot];
    }

    process->state = TASK_LOADING;
    process->saved_stack = 0;
    process->code_base = PROCESS_BASE;
    process->code_size = 0;
    process->entry_offset = 0;
    process->heap_pages = 0;
    process->parent_process_id = 0;
    process->wait_process_id = 0;
    process->exit_status = 255;
    process->window_handle = 0;
    process->page_directory = 0;
    process->keyboard_event_read = 0;
    process->keyboard_event_write = 0;
    process->standard_input_read = 0;
    process->standard_input_write = 0;
    process->keyboard_shift_pressed = 0;
    process->keyboard_caps_lock = 0;
    process->keyboard_extended = 0;
    process->standard_input_handle = STD_INPUT_HANDLE;
    process->standard_output_handle = STD_OUTPUT_HANDLE;
    process->standard_error_handle = STD_ERROR_HANDLE;
    process->background = (unsigned char)(background != 0);
    process->input_pipe = 0;
    process->output_pipe = 0;
    for (index = 0; index <= argument_length; ++index)
    {
        process->arguments[index] = arguments[index];
    }
    for (index = 0; filename[index] != '\0'; ++index)
    {
        process->program_path[index] = filename[index];
    }
    process->program_path[index] = '\0';
    for (index = 0; index < PROCESS_ENV_COUNT; ++index)
    {
        process->environment[index][0] = '\0';
    }
    {
        static const char default_path[] = "PATH=A:/Programs";
        for (index = 0; index < sizeof(default_path); ++index)
        {
            process->environment[0][index] = default_path[index];
        }
    }

    process->page_directory = memory_create_address_space();
    if (process->page_directory == 0)
    {
        int released = process_abort(process, slot);
        restore_interrupts(flags);
        return released
            ? PROCESS_START_NO_MEMORY
            : PROCESS_START_CLEANUP_ERROR;
    }

    for (index = 0;
        index < EXECUTABLE_PAGE_COUNT + PROCESS_STACK_PAGE_COUNT;
        ++index)
    {
        unsigned int physical_address = memory_allocate_page();
        unsigned int virtual_address = index < EXECUTABLE_PAGE_COUNT
            ? PROCESS_BASE + index * 4096
            : PROCESS_BASE + PROCESS_SLOT_SIZE -
                PROCESS_STACK_PAGE_COUNT * 4096 +
                (index - EXECUTABLE_PAGE_COUNT) * 4096;

        if (physical_address == 0 ||
            !memory_map_page_in(
                process->page_directory,
                virtual_address,
                physical_address,
                USER_PAGE_FLAGS))
        {
            int released;

            if (physical_address != 0)
            {
                memory_free_page(physical_address);
            }
            released = process_abort(process, slot);
            restore_interrupts(flags);
            return released
                ? PROCESS_START_NO_MEMORY
                : PROCESS_START_CLEANUP_ERROR;
        }
        {
            volatile unsigned int *page =
                (volatile unsigned int *)physical_address;
            unsigned int word;

            for (word = 0; word < 4096 / sizeof(unsigned int); ++word)
            {
                page[word] = 0;
            }
        }
    }

    previous_directory = memory_current_address_space();
    memory_switch_address_space(process->page_directory);
    read_result = filesystem_read_root_file(
        filename,
        (unsigned char *)process->code_base,
        EXECUTABLE_MAX_SIZE,
        &file_size);
    if (read_result != FILE_READ_OK)
    {
        int released;

        memory_switch_address_space(previous_directory);
        released = process_abort(process, slot);
        restore_interrupts(flags);
        if (!released)
        {
            return PROCESS_START_CLEANUP_ERROR;
        }
        switch (read_result)
        {
        case FILE_READ_INVALID_NAME:
            return PROCESS_START_INVALID_NAME;
        case FILE_READ_NOT_FOUND:
            return PROCESS_START_NOT_FOUND;
        case FILE_READ_TOO_LARGE:
            return PROCESS_START_TOO_LARGE;
        case FILE_READ_DISK_ERROR:
            return PROCESS_START_DISK_ERROR;
        default:
            return PROCESS_START_INVALID_EXECUTABLE;
        }
    }

    if (file_size >= 4 &&
        ((const unsigned char *)process->code_base)[0] == 0x7F &&
        ((const unsigned char *)process->code_base)[1] == 'E' &&
        ((const unsigned char *)process->code_base)[2] == 'L' &&
        ((const unsigned char *)process->code_base)[3] == 'F')
    {
        int loaded = process_load_elf(
            file_size, &entry_offset, &expected_size);

        if (loaded != 1)
        {
            int released;

            memory_switch_address_space(previous_directory);
            released = process_abort(process, slot);
            restore_interrupts(flags);
            return !released ? PROCESS_START_CLEANUP_ERROR :
                loaded < 0 ? PROCESS_START_NO_MEMORY :
                PROCESS_START_INVALID_EXECUTABLE;
        }
    }
    else
    {
        if (file_size < 12 ||
            ((const unsigned char *)process->code_base)[0] != 'M' ||
            ((const unsigned char *)process->code_base)[1] != 'E' ||
            ((const unsigned char *)process->code_base)[2] != 'X' ||
            ((const unsigned char *)process->code_base)[3] != 'E')
        {
            int released;

            memory_switch_address_space(previous_directory);
            released = process_abort(process, slot);
            restore_interrupts(flags);
            return released
                ? PROCESS_START_INVALID_EXECUTABLE
                : PROCESS_START_CLEANUP_ERROR;
        }
        expected_size = read_little_endian_dword(
            (const unsigned char *)process->code_base + 4);
        entry_offset = read_little_endian_dword(
            (const unsigned char *)process->code_base + 8);
        if (expected_size != file_size || entry_offset < 12 ||
            entry_offset >= file_size)
        {
            int released;

            memory_switch_address_space(previous_directory);
            released = process_abort(process, slot);
            restore_interrupts(flags);
            return released
                ? PROCESS_START_INVALID_EXECUTABLE
                : PROCESS_START_CLEANUP_ERROR;
        }
    }
    memory_switch_address_space(previous_directory);
    process->code_size = expected_size;
    process->entry_offset = entry_offset;
    expected_size = (process->code_size + 4095) / 4096;
    for (index = expected_size; index < EXECUTABLE_PAGE_COUNT; ++index)
    {
        unsigned int unmapped_address;

        if (!memory_unmap_page_in(
                process->page_directory,
                process->code_base + index * 4096,
                &unmapped_address) ||
            !memory_free_page(unmapped_address))
        {
            process->state = TASK_CLEANUP_ERROR;
            restore_interrupts(flags);
            return PROCESS_START_NO_MEMORY;
        }
    }

    pid = next_process_id++;
    if (next_process_id == 0)
    {
        next_process_id = 1;
    }
    process->process_id = pid;
    for (index = 0; filename[index] != '\0'; ++index)
    {
        if (filename[index] == '/' || filename[index] == ':')
        {
            base_start = index + 1;
        }
    }
    for (index = 0; index < sizeof(process->name) - 1 &&
        filename[base_start + index] != '\0'; ++index)
    {
        process->name[index] = filename[base_start + index];
    }
    process->name[index] = '\0';

    stack_top = process_kernel_stack_top(process);
    stack = (unsigned int *)(stack_top - sizeof(SavedContext));
    stack[0] = USER_DATA_SELECTOR;
    stack[1] = USER_DATA_SELECTOR;
    stack[2] = USER_DATA_SELECTOR;
    stack[3] = USER_DATA_SELECTOR;
    stack[4] = 0;
    stack[5] = 0;
    stack[6] = 0;
    stack[7] = 0;
    stack[8] = 0;
    stack[9] = 0;
    stack[10] = 0;
    stack[11] = 0;
    stack[12] = process->code_base + entry_offset;
    stack[13] = USER_CODE_SELECTOR;
    stack[14] = 0x202;
    stack[15] = PROCESS_BASE + PROCESS_SLOT_SIZE - 16;
    stack[16] = USER_DATA_SELECTOR;
    process->saved_stack = stack;
    *process_id = pid;
    process->state = TASK_ACTIVE;
    kernel_log_value(KERNEL_LOG_INFO, "process", "started pid", pid);
    restore_interrupts(flags);
    return PROCESS_START_OK;
}

// Starts a program for the shell. Returns the PID, or the negated
// ProcessStartResult on failure. A SPAWN_PIPE_OUTPUT start must be followed
// by a SPAWN_PIPE_INPUT start to complete a pipeline.
int process_spawn(
    const Process *parent,
    const char *filename,
    const char *arguments,
    unsigned int spawn_flags)
{
    unsigned int flags = disable_interrupts();
    unsigned int child_process_id;
    int background = (spawn_flags & SPAWN_BACKGROUND) != 0;
    Process *child;
    enum ProcessStartResult result;

    if (spawn_flags & SPAWN_PIPE_OUTPUT)
    {
        if (process_pipe.active)
        {
            restore_interrupts(flags);
            return -(int)PROCESS_START_LIMIT_REACHED;
        }
        process_pipe.active = 1;
        process_pipe.producer_id = 0;
        process_pipe.consumer_id = 0;
        process_pipe.read_position = 0;
        process_pipe.write_position = 0;
        process_pipe.buffered = 0;
        process_pipe.producer_closed = 0;
        process_pipe.consumer_closed = 0;
        process_pipe.overflowed = 0;
    }
    else if ((spawn_flags & SPAWN_PIPE_INPUT) &&
        (!process_pipe.active || process_pipe.producer_id == 0 ||
            process_pipe.consumer_id != 0))
    {
        restore_interrupts(flags);
        return -(int)PROCESS_START_CLEANUP_ERROR;
    }

    result = process_start(
        filename, arguments, &child_process_id, background);
    child = result == PROCESS_START_OK ?
        process_find_active(child_process_id) : 0;
    if (child == 0)
    {
        if (spawn_flags & SPAWN_PIPE_OUTPUT)
        {
            process_pipe.active = 0;
        }
        if (spawn_flags & SPAWN_PIPE_INPUT)
        {
            Process *producer = process_find_active(process_pipe.producer_id);

            process_pipe.active = 0;
            if (producer != 0)
            {
                producer->state = TASK_DYING;
            }
        }
        restore_interrupts(flags);
        return -(int)(result == PROCESS_START_OK ?
            PROCESS_START_CLEANUP_ERROR : result);
    }

    if (!background)
    {
        child->parent_process_id = parent->process_id;
    }
    process_inherit_environment(child, parent);
    if (spawn_flags & SPAWN_PIPE_OUTPUT)
    {
        process_pipe.producer_id = child_process_id;
        child->output_pipe = 1;
    }
    if (spawn_flags & SPAWN_PIPE_INPUT)
    {
        process_pipe.consumer_id = child_process_id;
        child->input_pipe = 1;
    }
    restore_interrupts(flags);
    return (int)child_process_id;
}
