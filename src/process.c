#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process.h"

#define PROCESS_LIMIT 8
#define TASK_LIMIT (PROCESS_LIMIT + 1)
#define EXECUTABLE_MAX_SIZE (64 * 1024)
#define EXECUTABLE_PAGE_COUNT (EXECUTABLE_MAX_SIZE / 4096)
#define PROCESS_STACK_PAGE_COUNT 4
#define PROCESS_SLOT_SIZE 0x00200000
#define PROCESS_BASE 0x40000000
#define PROCESS_PIPE_CAPACITY 16384
#define PROCESS_SYSCALL_PRINT 1
#define PROCESS_SYSCALL_KEYBOARD_READ 2
#define PROCESS_SYSCALL_EXIT 0
#define PROCESS_KEYBOARD_QUEUE_SIZE 16
#define PROCESS_KEYBOARD_QUEUE_MASK (PROCESS_KEYBOARD_QUEUE_SIZE - 1)
#define PROCESS_STANDARD_INPUT_QUEUE_SIZE 64
#define PROCESS_STANDARD_INPUT_QUEUE_MASK \
    (PROCESS_STANDARD_INPUT_QUEUE_SIZE - 1)

enum TaskState
{
    TASK_UNUSED,
    TASK_LOADING,
    TASK_ACTIVE,
    TASK_WAITING,
    TASK_DYING,
    TASK_EXITED,
    TASK_CLEANUP_ERROR
};

typedef struct
{
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int original_esp;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
} SavedContext;

typedef struct
{
    enum TaskState state;
    unsigned int process_id;
    unsigned int parent_process_id;
    unsigned int wait_process_id;
    unsigned int *saved_stack;
    unsigned int code_base;
    unsigned int code_size;
    unsigned int entry_offset;
    unsigned int code_page_count;
    unsigned int code_pages[EXECUTABLE_PAGE_COUNT];
    unsigned int stack_page_count;
    unsigned int stack_pages[PROCESS_STACK_PAGE_COUNT];
    volatile unsigned char keyboard_events[PROCESS_KEYBOARD_QUEUE_SIZE];
    volatile unsigned char keyboard_event_read;
    volatile unsigned char keyboard_event_write;
    volatile unsigned char standard_input[PROCESS_STANDARD_INPUT_QUEUE_SIZE];
    volatile unsigned char standard_input_read;
    volatile unsigned char standard_input_write;
    unsigned char keyboard_shift_pressed;
    unsigned char keyboard_caps_lock;
    unsigned char keyboard_extended;
    char arguments[COMMAND_BUFFER_SIZE];
    char name[13];
    unsigned int standard_input_handle;
    unsigned int standard_output_handle;
    unsigned int standard_error_handle;
    unsigned char background;
    unsigned int input_pipe;
    unsigned int output_pipe;
} Process;

typedef struct
{
    int active;
    unsigned int producer_id;
    unsigned int consumer_id;
    unsigned int read_position;
    unsigned int write_position;
    unsigned int buffered;
    unsigned char producer_closed;
    unsigned char consumer_closed;
    unsigned char overflowed;
    unsigned char data[PROCESS_PIPE_CAPACITY];
} ProcessPipe;

static Process processes[TASK_LIMIT];
static ProcessPipe process_pipe;
static unsigned int current_task;
static unsigned int next_process_id = 1;

static unsigned int disable_interrupts(void)
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

static void restore_interrupts(unsigned int flags)
{
    __asm
    {
        push flags
        popfd
    }
}

static unsigned int read_little_endian_dword(const unsigned char *data)
{
    return (unsigned int)data[0] |
        ((unsigned int)data[1] << 8) |
        ((unsigned int)data[2] << 16) |
        ((unsigned int)data[3] << 24);
}

static unsigned int process_slot_base(unsigned int slot)
{
    return PROCESS_BASE + slot * PROCESS_SLOT_SIZE;
}

static int process_release_mapping(
    unsigned int virtual_address,
    unsigned int physical_address)
{
    unsigned int unmapped_address;

    return memory_unmap_page(virtual_address, &unmapped_address) &&
        unmapped_address == physical_address &&
        memory_free_page(physical_address);
}

static int process_release(Process *process, unsigned int slot)
{
    unsigned int index;
    int success = 1;

    for (index = 0; index < process->code_page_count; ++index)
    {
        if (!process_release_mapping(
            process->code_base + index * 4096,
            process->code_pages[index]))
        {
            success = 0;
        }
    }

    for (index = 0; index < process->stack_page_count; ++index)
    {
        unsigned int stack_base =
            process_slot_base(slot) + PROCESS_SLOT_SIZE -
            PROCESS_STACK_PAGE_COUNT * 4096;
        if (!process_release_mapping(
            stack_base + index * 4096,
            process->stack_pages[index]))
        {
            success = 0;
        }
    }

    process->code_page_count = 0;
    process->stack_page_count = 0;
    process->saved_stack = 0;
    process->code_size = 0;
    process->entry_offset = 0;
    process->arguments[0] = '\0';
    return success;
}

static int process_abort(Process *process, unsigned int slot)
{
    if (process_release(process, slot))
    {
        process->state = TASK_UNUSED;
        return 1;
    }

    process->state = TASK_CLEANUP_ERROR;
    return 0;
}

static void process_cleanup_terminated(void)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_DYING)
        {
            if (process_release(&processes[slot], slot))
            {
                kernel_log_value(
                    KERNEL_LOG_INFO,
                    "process",
                    "cleaned up pid",
                    processes[slot].process_id);
                if (processes[slot].parent_process_id == 0)
                {
                    processes[slot].state = TASK_UNUSED;
                }
                else
                {
                    unsigned int waiter_slot;
                    unsigned int child_id = processes[slot].process_id;
                    unsigned int parent_id =
                        processes[slot].parent_process_id;

                    processes[slot].state = TASK_EXITED;
                    for (waiter_slot = 1;
                        waiter_slot < TASK_LIMIT;
                        ++waiter_slot)
                    {
                        Process *waiter = &processes[waiter_slot];

                        if (waiter->state == TASK_WAITING &&
                            waiter->process_id == parent_id &&
                            waiter->wait_process_id == child_id)
                        {
                            waiter->saved_stack[11] = 1;
                            waiter->wait_process_id = 0;
                            waiter->state = TASK_ACTIVE;
                            processes[slot].parent_process_id = 0;
                            processes[slot].state = TASK_UNUSED;
                            kernel_log_value(
                                KERNEL_LOG_INFO,
                                "process",
                                "wait completed for pid",
                                child_id);
                            break;
                        }
                    }
                }
                video_terminal_restore();
            }
            else
            {
                unsigned int waiter_slot;
                unsigned int child_id = processes[slot].process_id;
                unsigned int parent_id =
                    processes[slot].parent_process_id;

                processes[slot].state = TASK_CLEANUP_ERROR;
                kernel_log_value(
                    KERNEL_LOG_ERROR,
                    "process",
                    "memory cleanup failed for pid",
                    processes[slot].process_id);
                terminal_write("process: failed to release task memory.\n");
                for (waiter_slot = 1;
                    waiter_slot < TASK_LIMIT;
                    ++waiter_slot)
                {
                    Process *waiter = &processes[waiter_slot];

                    if (waiter->state == TASK_WAITING &&
                        waiter->process_id == parent_id &&
                        waiter->wait_process_id == child_id)
                    {
                        waiter->saved_stack[11] = 0;
                        waiter->wait_process_id = 0;
                        waiter->state = TASK_ACTIVE;
                        break;
                    }
                }
            }
        }
    }
}

static unsigned int find_next_active_task(void)
{
    unsigned int distance;

    for (distance = 1; distance <= TASK_LIMIT; ++distance)
    {
        unsigned int candidate =
            (current_task + distance) % TASK_LIMIT;

        if (processes[candidate].state == TASK_ACTIVE &&
            processes[candidate].saved_stack != 0)
        {
            return candidate;
        }
    }
    return current_task;
}

static int process_keyboard_try_read_scancode(
    Process *process,
    unsigned char *scancode)
{
    if (process->keyboard_event_read == process->keyboard_event_write)
    {
        return 0;
    }

    *scancode = process->keyboard_events[process->keyboard_event_read];
    process->keyboard_event_read = (unsigned char)(
        (process->keyboard_event_read + 1) & PROCESS_KEYBOARD_QUEUE_MASK);
    return 1;
}

static int process_pipe_write(
    unsigned int pipe_handle,
    const unsigned char *data,
    unsigned int length)
{
    unsigned int index;

    if (pipe_handle != 1 || !process_pipe.active ||
        process_pipe.consumer_closed || process_pipe.overflowed)
    {
        return 0;
    }

    for (index = 0; index < length; ++index)
    {
        if (process_pipe.buffered == PROCESS_PIPE_CAPACITY)
        {
            process_pipe.overflowed = 1;
            return 0;
        }
        process_pipe.data[process_pipe.write_position] = data[index];
        process_pipe.write_position =
            (process_pipe.write_position + 1) % PROCESS_PIPE_CAPACITY;
        ++process_pipe.buffered;
    }
    return 1;
}

static int process_pipe_read(
    unsigned int pipe_handle,
    unsigned int *destination,
    unsigned int capacity)
{
    unsigned int bytes_to_read;
    unsigned int index;

    if (pipe_handle != 1 || !process_pipe.active ||
        process_pipe.overflowed)
    {
        return -1;
    }
    if (process_pipe.buffered == 0)
    {
        return process_pipe.producer_closed ? -1 : 0;
    }

    bytes_to_read = process_pipe.buffered;
    if (bytes_to_read > capacity)
    {
        bytes_to_read = capacity;
    }
    for (index = 0; index < bytes_to_read; ++index)
    {
        destination[index] = process_pipe.data[process_pipe.read_position];
        process_pipe.read_position =
            (process_pipe.read_position + 1) % PROCESS_PIPE_CAPACITY;
    }
    process_pipe.buffered -= bytes_to_read;
    return (int)bytes_to_read;
}

static int process_stream_read(
    Process *process,
    unsigned int handle,
    unsigned int *destination,
    unsigned int capacity)
{
    unsigned int bytes_read = 0;

    if (handle != process->standard_input_handle)
    {
        return -1;
    }

    if (process->input_pipe != 0)
    {
        return process_pipe_read(
            process->input_pipe, destination, capacity);
    }

    while (bytes_read < capacity)
    {
        if (process->standard_input_read ==
            process->standard_input_write)
        {
            break;
        }
        destination[bytes_read++] =
            process->standard_input[process->standard_input_read];
        process->standard_input_read = (unsigned char)(
            (process->standard_input_read + 1) &
            PROCESS_STANDARD_INPUT_QUEUE_MASK);
    }
    return (int)bytes_read;
}

static int process_stream_write(
    Process *process,
    unsigned int handle,
    const unsigned char *data,
    unsigned int length)
{
    unsigned int index;

    if (handle == process->standard_output_handle &&
        process->output_pipe != 0)
    {
        if (!process_pipe_write(
            process->output_pipe, data, length))
        {
            return -1;
        }
        return (int)length;
    }

    if (handle != process->standard_output_handle &&
        handle != process->standard_error_handle)
    {
        return -1;
    }
    for (index = 0; index < length; ++index)
    {
        terminal_write_character((char)data[index]);
    }
    return (int)length;
}

static void process_pipe_close_endpoint(const Process *process)
{
    if (!process_pipe.active)
    {
        return;
    }
    if (process->process_id == process_pipe.producer_id)
    {
        process_pipe.producer_closed = 1;
    }
    if (process->process_id == process_pipe.consumer_id)
    {
        process_pipe.consumer_closed = 1;
    }
    if (process_pipe.producer_closed && process_pipe.consumer_closed)
    {
        process_pipe.active = 0;
    }
}

static void process_terminate(Process *process)
{
    unsigned int slot;

    if (process->state != TASK_ACTIVE)
    {
        return;
    }

    kernel_log_value(
        KERNEL_LOG_INFO,
        "process",
        "exiting pid",
        process->process_id);
    filesystem_close_process_directories(process->process_id);
    filesystem_close_process_files(process->process_id);
    process_pipe_close_endpoint(process);
    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *child = &processes[slot];

        if (child->parent_process_id == process->process_id)
        {
            child->parent_process_id = 0;
            if (child->state == TASK_EXITED)
            {
                child->state = TASK_UNUSED;
            }
        }
    }
    process->state = TASK_DYING;
}

static void process_terminate_current(void)
{
    if (current_task != 0 &&
        processes[current_task].state == TASK_ACTIVE)
    {
        process_terminate(&processes[current_task]);
        current_task = find_next_active_task();
    }
}

static int process_print_string(
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

static int process_stack_buffer_is_valid(
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

static int process_copy_user_path(
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

static int process_copy_string_to_array(
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

void processes_initialize(void)
{
    unsigned int slot;

    for (slot = 0; slot < TASK_LIMIT; ++slot)
    {
        processes[slot].state = TASK_UNUSED;
        processes[slot].parent_process_id = 0;
        processes[slot].wait_process_id = 0;
        processes[slot].saved_stack = 0;
        processes[slot].code_page_count = 0;
        processes[slot].stack_page_count = 0;
        processes[slot].keyboard_event_read = 0;
        processes[slot].keyboard_event_write = 0;
        processes[slot].standard_input_read = 0;
        processes[slot].standard_input_write = 0;
        processes[slot].keyboard_shift_pressed = 0;
        processes[slot].keyboard_caps_lock = 0;
        processes[slot].keyboard_extended = 0;
        processes[slot].arguments[0] = '\0';
        processes[slot].standard_input_handle = STD_INPUT_HANDLE;
        processes[slot].standard_output_handle = STD_OUTPUT_HANDLE;
        processes[slot].standard_error_handle = STD_ERROR_HANDLE;
        processes[slot].background = 0;
        processes[slot].input_pipe = 0;
        processes[slot].output_pipe = 0;
    }

    process_pipe.active = 0;
    processes[0].state = TASK_ACTIVE;
    processes[0].process_id = 0;
    processes[0].name[0] = 's';
    processes[0].name[1] = 'h';
    processes[0].name[2] = 'e';
    processes[0].name[3] = 'l';
    processes[0].name[4] = 'l';
    processes[0].name[5] = '\0';
    current_task = 0;
    kernel_log(KERNEL_LOG_INFO, "process", "scheduler initialized");
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
    unsigned int *stack;
    unsigned int argument_length = 0;
    enum FileReadResult read_result;
    Process *process = 0;

    if (arguments == 0)
    {
        arguments = "";
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
        restore_interrupts(flags);
        return PROCESS_START_LIMIT_REACHED;
    }

    process->state = TASK_LOADING;
    process->saved_stack = 0;
    process->code_base = process_slot_base(slot);
    process->code_size = 0;
    process->entry_offset = 0;
    process->parent_process_id = 0;
    process->wait_process_id = 0;
    process->code_page_count = 0;
    process->stack_page_count = 0;
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

    for (index = 0; index < EXECUTABLE_PAGE_COUNT; ++index)
    {
        unsigned int physical_address = memory_allocate_page();

        if (physical_address == 0)
        {
            int released = process_abort(process, slot);
            restore_interrupts(flags);
            return released
                ? PROCESS_START_NO_MEMORY
                : PROCESS_START_CLEANUP_ERROR;
        }
        if (!memory_map_page(
            process->code_base + index * 4096,
            physical_address,
            0x02))
        {
            memory_free_page(physical_address);
            {
                int released = process_abort(process, slot);
                restore_interrupts(flags);
                return released
                    ? PROCESS_START_NO_MEMORY
                    : PROCESS_START_CLEANUP_ERROR;
            }
        }
        process->code_pages[process->code_page_count++] = physical_address;
    }

    for (index = 0; index < PROCESS_STACK_PAGE_COUNT; ++index)
    {
        unsigned int physical_address = memory_allocate_page();
        unsigned int stack_base =
            process->code_base + PROCESS_SLOT_SIZE -
            PROCESS_STACK_PAGE_COUNT * 4096;

        if (physical_address == 0)
        {
            int released = process_abort(process, slot);
            restore_interrupts(flags);
            return released
                ? PROCESS_START_NO_MEMORY
                : PROCESS_START_CLEANUP_ERROR;
        }
        if (!memory_map_page(
            stack_base + index * 4096,
            physical_address,
            0x02))
        {
            memory_free_page(physical_address);
            {
                int released = process_abort(process, slot);
                restore_interrupts(flags);
                return released
                    ? PROCESS_START_NO_MEMORY
                    : PROCESS_START_CLEANUP_ERROR;
            }
        }
        process->stack_pages[process->stack_page_count++] = physical_address;
    }

    read_result = filesystem_read_root_file(
        filename,
        (unsigned char *)process->code_base,
        EXECUTABLE_MAX_SIZE,
        &file_size);
    if (read_result != FILE_READ_OK)
    {
        int released = process_abort(process, slot);
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

    if (file_size < 12 ||
        ((const unsigned char *)process->code_base)[0] != 'M' ||
        ((const unsigned char *)process->code_base)[1] != 'E' ||
        ((const unsigned char *)process->code_base)[2] != 'X' ||
        ((const unsigned char *)process->code_base)[3] != 'E')
    {
        int released = process_abort(process, slot);
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
        int released = process_abort(process, slot);
        restore_interrupts(flags);
        return released
            ? PROCESS_START_INVALID_EXECUTABLE
            : PROCESS_START_CLEANUP_ERROR;
    }

    process->code_size = file_size;
    process->entry_offset = entry_offset;
    expected_size = (file_size + 4095) / 4096;
    while (process->code_page_count > expected_size)
    {
        index = process->code_page_count - 1;
        if (!process_release_mapping(
            process->code_base + index * 4096,
            process->code_pages[index]))
        {
            process->state = TASK_CLEANUP_ERROR;
            restore_interrupts(flags);
            return PROCESS_START_NO_MEMORY;
        }
        --process->code_page_count;
    }

    pid = next_process_id++;
    if (next_process_id == 0)
    {
        next_process_id = 1;
    }
    process->process_id = pid;
    for (index = 0; index < sizeof(process->name) - 1 &&
        filename[index] != '\0'; ++index)
    {
        process->name[index] = filename[index];
    }
    process->name[index] = '\0';

    stack_top = process->code_base + PROCESS_SLOT_SIZE;
    stack = (unsigned int *)(stack_top - sizeof(SavedContext));
    stack[0] = 0x10;
    stack[1] = 0x10;
    stack[2] = 0x10;
    stack[3] = 0x10;
    stack[4] = 0;
    stack[5] = 0;
    stack[6] = 0;
    stack[7] = stack_top;
    stack[8] = 0;
    stack[9] = 0;
    stack[10] = 0;
    stack[11] = 0;
    stack[12] = process->code_base + entry_offset;
    stack[13] = 0x08;
    stack[14] = 0x202;
    process->saved_stack = stack;
    *process_id = pid;
    process->state = TASK_ACTIVE;
    kernel_log_value(KERNEL_LOG_INFO, "process", "started pid", pid);
    restore_interrupts(flags);
    return PROCESS_START_OK;
}

enum ProcessStartResult process_start_pipeline(
    const char *first_filename,
    const char *first_arguments,
    const char *second_filename,
    const char *second_arguments,
    int background)
{
    unsigned int flags = disable_interrupts();
    unsigned int first_process_id;
    unsigned int second_process_id;
    unsigned int index;
    Process *first_process = 0;
    Process *second_process = 0;
    enum ProcessStartResult result;

    if (process_pipe.active)
    {
        restore_interrupts(flags);
        return PROCESS_START_LIMIT_REACHED;
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

    result = process_start(
        first_filename, first_arguments, &first_process_id, background);
    if (result != PROCESS_START_OK)
    {
        process_pipe.active = 0;
        restore_interrupts(flags);
        return result;
    }

    for (index = 1; index < TASK_LIMIT; ++index)
    {
        if (processes[index].process_id == first_process_id &&
            processes[index].state == TASK_ACTIVE)
        {
            first_process = &processes[index];
            break;
        }
    }
    if (first_process == 0)
    {
        process_pipe.active = 0;
        restore_interrupts(flags);
        return PROCESS_START_CLEANUP_ERROR;
    }

    process_pipe.producer_id = first_process_id;
    first_process->output_pipe = 1;
    result = process_start(
        second_filename, second_arguments, &second_process_id, background);
    if (result != PROCESS_START_OK)
    {
        process_pipe.active = 0;
        first_process->state = TASK_DYING;
        restore_interrupts(flags);
        return result;
    }

    for (index = 1; index < TASK_LIMIT; ++index)
    {
        if (processes[index].process_id == second_process_id &&
            processes[index].state == TASK_ACTIVE)
        {
            second_process = &processes[index];
            break;
        }
    }
    if (second_process == 0)
    {
        process_pipe.active = 0;
        first_process->state = TASK_DYING;
        restore_interrupts(flags);
        return PROCESS_START_CLEANUP_ERROR;
    }

    process_pipe.consumer_id = second_process_id;
    second_process->input_pipe = 1;
    restore_interrupts(flags);
    return PROCESS_START_OK;
}

unsigned int __cdecl process_timer_tick(unsigned int *saved_stack)
{
    processes[current_task].saved_stack = saved_stack;
    __outbyte(0x20, 0x20);
    process_cleanup_terminated();
    current_task = find_next_active_task();
    return (unsigned int)processes[current_task].saved_stack;
}

unsigned int __cdecl process_system_call(unsigned int *saved_stack)
{
    Process *process;
    unsigned int task;
    unsigned int syscall_number;

    processes[current_task].saved_stack = saved_stack;
    if (current_task == 0 ||
        processes[current_task].state != TASK_ACTIVE)
    {
        return (unsigned int)saved_stack;
    }

    task = current_task;
    process = &processes[task];
    syscall_number = saved_stack[11];
    if (syscall_number == PROCESS_SYSCALL_PRINT)
    {
        if (!process_print_string(process, saved_stack[8]))
        {
            terminal_write("process: invalid print request; terminating.\n");
            process_terminate_current();
        }
    }
    else if (syscall_number == PROCESS_SYSCALL_KEYBOARD_READ)
    {
        unsigned char scancode;

        saved_stack[11] = 0;
        while (process_keyboard_try_read_scancode(process, &scancode))
        {
            saved_stack[11] = scancode;
            break;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_OPENDIR)
    {
        char path[128];

        if (process_copy_user_path(process, saved_stack[8], path))
        {
            saved_stack[11] = (unsigned int)filesystem_open_directory(
                process->process_id, path);
        }
        else
        {
            saved_stack[11] = (unsigned int)-1;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CLOSEDIR)
    {
        saved_stack[11] = (unsigned int)filesystem_close_directory(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_READ_DIR)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_read_directory(
                    process->process_id,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_ARGUMENTS ||
        syscall_number == PROCESS_SERVICE_GET_CURRENT_DIRECTORY)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 128 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else if (syscall_number == PROCESS_SERVICE_READ_ARGUMENTS)
            {
                saved_stack[11] = (unsigned int)
                    process_copy_string_to_array(
                        process->arguments,
                        (unsigned int *)saved_stack[10],
                        capacity);
            }
            else
            {
                char path[128];
                int length = filesystem_get_current_directory(
                    path, sizeof(path));

                saved_stack[11] = length < 0
                    ? (unsigned int)-1
                    : (unsigned int)process_copy_string_to_array(
                        path,
                        (unsigned int *)saved_stack[10],
                        capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CHANGE_DIRECTORY)
    {
        saved_stack[11] = (unsigned int)filesystem_change_directory(
            process->arguments);
    }
    else if (syscall_number == PROCESS_SERVICE_OPEN_FILE)
    {
        char path[128];

        if (process_copy_user_path(process, saved_stack[8], path))
        {
            saved_stack[11] = (unsigned int)filesystem_open_file(
                process->process_id, path);
        }
        else
        {
            saved_stack[11] = (unsigned int)-1;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_FILE)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_read_file(
                    process->process_id,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_CLOSE_FILE)
    {
        saved_stack[11] = (unsigned int)filesystem_close_file(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_FILE)
    {
        unsigned int length = saved_stack[9];
        unsigned int buffer_size;

        if (length > 256 ||
            length > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = length * sizeof(unsigned int);
            if (buffer_size != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)filesystem_write_file(
                    process->process_id,
                    saved_stack[8],
                    (const unsigned int *)saved_stack[10],
                    length);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_TRUNCATE_FILE)
    {
        saved_stack[11] = (unsigned int)filesystem_truncate_file(
            process->process_id, saved_stack[8]);
    }
    else if (syscall_number == PROCESS_SERVICE_KILL_PROCESS)
    {
        saved_stack[11] = (unsigned int)process_kill_by_name(
            process->process_id, process->arguments);
    }
    else if (syscall_number == PROCESS_SERVICE_CREATE_PROCESS)
    {
        char filename[128];
        unsigned int child_process_id;
        unsigned int child_slot;
        enum ProcessStartResult result;

        if (!process_copy_user_path(process, saved_stack[8], filename))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            result = process_start(
                filename,
                "",
                &child_process_id,
                process->background);
            if (result != PROCESS_START_OK)
            {
                kernel_log_value(
                    KERNEL_LOG_WARNING,
                    "process",
                    "create process failed with result",
                    (unsigned int)result);
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)-1;
                for (child_slot = 1;
                    child_slot < TASK_LIMIT;
                    ++child_slot)
                {
                    Process *child = &processes[child_slot];

                    if (child->state == TASK_ACTIVE &&
                        child->process_id == child_process_id)
                    {
                        child->parent_process_id = process->process_id;
                        saved_stack[11] = child_process_id;
                        break;
                    }
                }
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WAIT_PROCESS)
    {
        unsigned int child_process_id = saved_stack[8];
        unsigned int child_slot;

        saved_stack[11] = 0;
        for (child_slot = 1; child_slot < TASK_LIMIT; ++child_slot)
        {
            Process *child = &processes[child_slot];

            if (child->process_id != child_process_id ||
                child->parent_process_id != process->process_id)
            {
                continue;
            }

            if (child->state == TASK_EXITED)
            {
                child->parent_process_id = 0;
                child->state = TASK_UNUSED;
                saved_stack[11] = 1;
            }
            else if (child->state == TASK_ACTIVE ||
                child->state == TASK_WAITING ||
                child->state == TASK_DYING)
            {
                process->wait_process_id = child_process_id;
                saved_stack[11] = 0;
                process->state = TASK_WAITING;
                current_task = find_next_active_task();
            }
            break;
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_STREAM)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (capacity != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)process_stream_read(
                    process,
                    saved_stack[8],
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_STREAM)
    {
        unsigned int length = saved_stack[9];
        unsigned int buffer_size;

        if (length > 256 ||
            length > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = length * sizeof(unsigned int);
            if (length != 0 &&
                !process_stack_buffer_is_valid(
                    process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                unsigned char buffer[256];
                const unsigned int *source =
                    (const unsigned int *)saved_stack[10];
                unsigned int index;
                int valid = 1;

                for (index = 0; index < length; ++index)
                {
                    if (source[index] > 255)
                    {
                        valid = 0;
                        break;
                    }
                    buffer[index] = (unsigned char)source[index];
                }
                saved_stack[11] = valid
                    ? (unsigned int)process_stream_write(
                        process,
                        saved_stack[8],
                        buffer,
                        length)
                    : (unsigned int)-1;
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_READ_PIPE)
    {
        unsigned int capacity = saved_stack[9];
        unsigned int buffer_size;

        if (capacity > 256 ||
            capacity > 0xFFFFFFFF / sizeof(unsigned int))
        {
            saved_stack[11] = (unsigned int)-1;
        }
        else
        {
            buffer_size = capacity * sizeof(unsigned int);
            if (!process_stack_buffer_is_valid(
                process, saved_stack[10], buffer_size))
            {
                saved_stack[11] = (unsigned int)-1;
            }
            else
            {
                saved_stack[11] = (unsigned int)process_pipe_read(
                    process->input_pipe,
                    (unsigned int *)saved_stack[10],
                    capacity);
            }
        }
    }
    else if (syscall_number == PROCESS_SERVICE_WRITE_CHARACTER)
    {
        unsigned char character = (unsigned char)saved_stack[8];

        if (process_stream_write(
            process,
            process->standard_output_handle,
            &character,
            1) < 0)
        {
            terminal_write(
                "process: pipe buffer full or reader closed; terminating.\n");
            process_terminate_current();
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_CLEAR_GRAPHICS)
    {
        if (!process->background)
        {
            video_clear_graphics((unsigned char)saved_stack[8]);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_DRAW_PIXEL)
    {
        if (!process->background)
        {
            video_draw_pixel(
                saved_stack[8],
                saved_stack[9],
                (unsigned char)saved_stack[10]);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SERVICE_PRESENT_GRAPHICS)
    {
        if (!process->background)
        {
            video_present_graphics();
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number >= PROCESS_SERVICE_CLOCK &&
        syscall_number < PROCESS_SERVICE_WRITE_CHARACTER)
    {
        if (!process->background ||
            (syscall_number != PROCESS_SERVICE_TERMINAL_CLEAR &&
                syscall_number != PROCESS_SERVICE_START_GRAPHICS &&
                syscall_number != PROCESS_SERVICE_END_GRAPHICS))
        {
            shell_run_service(syscall_number, process->arguments);
        }
        saved_stack[11] = 0;
    }
    else if (syscall_number == PROCESS_SYSCALL_EXIT)
    {
        process_terminate_current();
    }
    else
    {
        terminal_write("process: unsupported system call; terminating.\n");
        process_terminate_current();
    }

    if (current_task == task && processes[task].state == TASK_ACTIVE)
    {
        /* Blocking services may be preempted; resume at the original syscall frame. */
        processes[task].saved_stack = saved_stack;
        return (unsigned int)saved_stack;
    }

    return (unsigned int)processes[current_task].saved_stack;
}

unsigned char process_keyboard_read_scancode(void)
{
    for (;;)
    {
        Process *process = &processes[current_task];
        unsigned char scancode;

        if (process_keyboard_try_read_scancode(process, &scancode))
        {
            return scancode;
        }
    }
}

void process_keyboard_scancode(unsigned char scancode)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *process = &processes[slot];
        unsigned char next;
        unsigned char next_input;
        char character;

        if (process->state != TASK_ACTIVE)
        {
            continue;
        }
        if (process->background)
        {
            continue;
        }

        character = keyboard_decode_scancode(
            scancode,
            &process->keyboard_shift_pressed,
            &process->keyboard_caps_lock,
            &process->keyboard_extended);
        if (character != '\0')
        {
            next_input = (unsigned char)(
                (process->standard_input_write + 1) &
                PROCESS_STANDARD_INPUT_QUEUE_MASK);
            if (next_input != process->standard_input_read)
            {
                process->standard_input[process->standard_input_write] =
                    (unsigned char)character;
                process->standard_input_write = next_input;
            }
        }

        next = (unsigned char)(
            (process->keyboard_event_write + 1) &
            PROCESS_KEYBOARD_QUEUE_MASK);
        if (next != process->keyboard_event_read)
        {
            process->keyboard_events[process->keyboard_event_write] =
                scancode;
            process->keyboard_event_write = next;
        }
    }
}

int process_has_active_process(void)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_ACTIVE)
        {
            return 1;
        }
    }

    return 0;
}

int process_has_foreground_process(void)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if ((processes[slot].state == TASK_ACTIVE ||
                processes[slot].state == TASK_WAITING ||
                processes[slot].state == TASK_DYING) &&
            !processes[slot].background)
        {
            return 1;
        }
    }

    return 0;
}

static int process_name_matches(
    const char *process_name,
    const char *requested_name)
{
    const char *process_base = process_name;
    const char *requested_base = requested_name;
    unsigned int process_length = 0;
    unsigned int requested_length = 0;
    unsigned int index;

    while (*process_name != '\0')
    {
        if (*process_name == '/' || *process_name == '\\' ||
            *process_name == ':')
        {
            process_base = process_name + 1;
        }
        ++process_name;
    }
    while (*requested_name != '\0')
    {
        if (*requested_name == '/' || *requested_name == '\\' ||
            *requested_name == ':')
        {
            requested_base = requested_name + 1;
        }
        ++requested_name;
    }

    while (process_base[process_length] != '\0')
    {
        ++process_length;
    }
    while (requested_base[requested_length] != '\0')
    {
        ++requested_length;
    }
    while (requested_length != 0 &&
        is_space(requested_base[requested_length - 1]))
    {
        --requested_length;
    }

    if (process_length >= 4 &&
        process_base[process_length - 4] == '.' &&
        (process_base[process_length - 3] == 'e' ||
            process_base[process_length - 3] == 'E') &&
        (process_base[process_length - 2] == 'x' ||
            process_base[process_length - 2] == 'X') &&
        (process_base[process_length - 1] == 'e' ||
            process_base[process_length - 1] == 'E'))
    {
        process_length -= 4;
    }
    if (requested_length >= 4 &&
        requested_base[requested_length - 4] == '.' &&
        (requested_base[requested_length - 3] == 'e' ||
            requested_base[requested_length - 3] == 'E') &&
        (requested_base[requested_length - 2] == 'x' ||
            requested_base[requested_length - 2] == 'X') &&
        (requested_base[requested_length - 1] == 'e' ||
            requested_base[requested_length - 1] == 'E'))
    {
        requested_length -= 4;
    }
    if (process_length == 0 || process_length != requested_length)
    {
        return 0;
    }

    for (index = 0; index < process_length; ++index)
    {
        char left = process_base[index];
        char right = requested_base[index];

        if (left >= 'A' && left <= 'Z')
        {
            left = (char)(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z')
        {
            right = (char)(right - 'A' + 'a');
        }
        if (left != right)
        {
            return 0;
        }
    }

    return 1;
}

int process_kill_by_name(
    unsigned int caller_process_id,
    const char *name)
{
    unsigned int slot;

    while (is_space(*name))
    {
        ++name;
    }
    if (*name == '\0')
    {
        return -1;
    }

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *process = &processes[slot];

        if (process->state == TASK_ACTIVE &&
            process->process_id != caller_process_id &&
            process_name_matches(process->name, name))
        {
            process_terminate(process);
            return 1;
        }
    }

    return 0;
}

void process_list(void)
{
    unsigned int flags = disable_interrupts();
    unsigned int slot;

    terminal_write("PID  TID  STATE   NAME\n");
    for (slot = 0; slot < TASK_LIMIT; ++slot)
    {
        Process *process = &processes[slot];

        if (process->state == TASK_UNUSED)
        {
            continue;
        }

        terminal_write_unsigned(process->process_id);
        terminal_write("    ");
        terminal_write_unsigned(process->process_id);
        terminal_write("    ");
        switch (process->state)
        {
        case TASK_LOADING:
            terminal_write("LOADING ");
            break;
        case TASK_ACTIVE:
            if (process->background)
            {
                terminal_write("BACKGROUND ");
            }
            else
            {
                terminal_write(slot == current_task ? "RUNNING " : "READY   ");
            }
            break;
        case TASK_WAITING:
            terminal_write("WAITING ");
            break;
        case TASK_DYING:
            terminal_write("EXITING ");
            break;
        case TASK_EXITED:
            terminal_write("EXITED  ");
            break;
        default:
            terminal_write("ERROR   ");
            break;
        }
        terminal_write(process->name);
        terminal_write("\n");
    }
    restore_interrupts(flags);
}
