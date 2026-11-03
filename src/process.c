#include <intrin.h>
#include "kernel.h"

#define PROCESS_LIMIT 8
#define TASK_LIMIT (PROCESS_LIMIT + 1)
#define EXECUTABLE_MAX_SIZE (64 * 1024)
#define EXECUTABLE_PAGE_COUNT (EXECUTABLE_MAX_SIZE / 4096)
#define PROCESS_STACK_PAGE_COUNT 4
#define PROCESS_SLOT_SIZE 0x00200000
#define PROCESS_BASE 0x40000000
#define PROCESS_SYSCALL_PRINT 1
#define PROCESS_SYSCALL_KEYBOARD_READ 2
#define PROCESS_SYSCALL_EXIT 0
#define PROCESS_KEYBOARD_QUEUE_SIZE 16
#define PROCESS_KEYBOARD_QUEUE_MASK (PROCESS_KEYBOARD_QUEUE_SIZE - 1)

enum TaskState
{
    TASK_UNUSED,
    TASK_LOADING,
    TASK_ACTIVE,
    TASK_DYING,
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
    unsigned int *saved_stack;
    unsigned int code_base;
    unsigned int code_size;
    unsigned int entry_offset;
    unsigned int code_page_count;
    unsigned int code_pages[EXECUTABLE_PAGE_COUNT];
    unsigned int stack_page_count;
    unsigned int stack_pages[PROCESS_STACK_PAGE_COUNT];
    unsigned char keyboard_events[PROCESS_KEYBOARD_QUEUE_SIZE];
    unsigned char keyboard_event_read;
    unsigned char keyboard_event_write;
    char name[13];
} Process;

static Process processes[TASK_LIMIT];
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
                processes[slot].state = TASK_UNUSED;
                video_terminal_restore();
            }
            else
            {
                processes[slot].state = TASK_CLEANUP_ERROR;
                terminal_write("process: failed to release task memory.\n");
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

static void process_terminate_current(void)
{
    if (current_task != 0 &&
        processes[current_task].state == TASK_ACTIVE)
    {
        processes[current_task].state = TASK_DYING;
        current_task = find_next_active_task();
    }
}

static int process_print_string(
    const Process *process,
    unsigned int address)
{
    const unsigned int end = process->code_base + process->code_size;
    const char *text;
    unsigned int index;

    if (address < process->code_base || address >= end)
    {
        return 0;
    }

    text = (const char *)address;
    for (index = address; index < end; ++index)
    {
        if (*(const char *)index == '\0')
        {
            terminal_write(text);
            return 1;
        }
    }
    return 0;
}

void processes_initialize(void)
{
    unsigned int slot;

    for (slot = 0; slot < TASK_LIMIT; ++slot)
    {
        processes[slot].state = TASK_UNUSED;
        processes[slot].saved_stack = 0;
        processes[slot].code_page_count = 0;
        processes[slot].stack_page_count = 0;
        processes[slot].keyboard_event_read = 0;
        processes[slot].keyboard_event_write = 0;
    }

    processes[0].state = TASK_ACTIVE;
    processes[0].process_id = 0;
    processes[0].name[0] = 's';
    processes[0].name[1] = 'h';
    processes[0].name[2] = 'e';
    processes[0].name[3] = 'l';
    processes[0].name[4] = 'l';
    processes[0].name[5] = '\0';
    current_task = 0;
}

enum ProcessStartResult process_start(
    const char *filename,
    unsigned int *process_id)
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
    enum FileReadResult read_result;
    Process *process = 0;

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
    process->code_page_count = 0;
    process->stack_page_count = 0;
    process->keyboard_event_read = 0;
    process->keyboard_event_write = 0;

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
    terminal_write("Started ");
    terminal_write(process->name);
    terminal_write(" (PID ");
    terminal_write_unsigned(pid);
    terminal_write(")\n");
    process->state = TASK_ACTIVE;
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
    unsigned int syscall_number;

    processes[current_task].saved_stack = saved_stack;
    if (current_task == 0 ||
        processes[current_task].state != TASK_ACTIVE)
    {
        return (unsigned int)saved_stack;
    }

    process = &processes[current_task];
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
        if (process->keyboard_event_read != process->keyboard_event_write)
        {
            saved_stack[11] =
                process->keyboard_events[process->keyboard_event_read];
            process->keyboard_event_read = (unsigned char)(
                (process->keyboard_event_read + 1) &
                PROCESS_KEYBOARD_QUEUE_MASK);
        }
        else
        {
            saved_stack[11] = 0;
        }
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

    return (unsigned int)processes[current_task].saved_stack;
}

void process_keyboard_scancode(unsigned char scancode)
{
    unsigned int slot;

    if ((scancode & 0x80) != 0)
    {
        return;
    }

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *process = &processes[slot];
        unsigned char next;

        if (process->state != TASK_ACTIVE)
        {
            continue;
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
            terminal_write(slot == current_task ? "RUNNING " : "READY   ");
            break;
        case TASK_DYING:
            terminal_write("EXITING ");
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
