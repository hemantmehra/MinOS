#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"
#include "loopback.h"
#include "desktop.h"

unsigned int process_count;
unsigned int process_table_mapped;
ProcessPipe process_pipe;
unsigned int current_task;
unsigned int next_process_id = 1;
volatile unsigned int tick_count;

int process_release(Process *process, unsigned int slot)
{
    (void)slot;
    if (process->page_directory != 0)
    {
        memory_destroy_address_space(process->page_directory);
        process->page_directory = 0;
    }

    process->saved_stack = 0;
    process->code_size = 0;
    process->entry_offset = 0;
    process->arguments[0] = '\0';
    return 1;
}

unsigned int process_kernel_stack_top(const Process *process)
{
    return (unsigned int)process + sizeof(Process);
}

/* Loads the page directory and ring 0 stack of the task about to run. */
void process_activate_current(void)
{
    Process *process = &processes[current_task];

    if (current_task == 0 || process->page_directory == 0)
    {
        memory_switch_address_space(memory_kernel_address_space());
        return;
    }
    interrupts_set_kernel_stack(process_kernel_stack_top(process));
    memory_switch_address_space(process->page_directory);
}

void process_initialize_slot(Process *process)
{
    process->state = TASK_UNUSED;
    process->process_id = 0;
    process->parent_process_id = 0;
    process->wait_process_id = 0;
    process->window_handle = 0;
    process->saved_stack = 0;
    process->page_directory = 0;
    process->keyboard_event_read = 0;
    process->keyboard_event_write = 0;
    process->standard_input_read = 0;
    process->standard_input_write = 0;
    process->keyboard_shift_pressed = 0;
    process->keyboard_caps_lock = 0;
    process->keyboard_extended = 0;
    process->arguments[0] = '\0';
    process->standard_input_handle = STD_INPUT_HANDLE;
    process->standard_output_handle = STD_OUTPUT_HANDLE;
    process->standard_error_handle = STD_ERROR_HANDLE;
    process->background = 0;
    process->input_pipe = 0;
    process->output_pipe = 0;
}

int process_table_grow(void)
{
    unsigned int required = (process_count + 1) * sizeof(Process);

    if (required > PROCESS_TABLE_SIZE)
    {
        return 0;
    }
    while (process_table_mapped < required)
    {
        unsigned int physical_address = memory_allocate_page();
        volatile unsigned int *page;
        unsigned int index;

        if (physical_address == 0)
        {
            return 0;
        }
        if (!memory_map_page(
            PROCESS_TABLE_BASE + process_table_mapped,
            physical_address,
            0x02))
        {
            memory_free_page(physical_address);
            return 0;
        }
        page = (volatile unsigned int *)(
            PROCESS_TABLE_BASE + process_table_mapped);
        for (index = 0; index < 1024; ++index)
        {
            page[index] = 0;
        }
        process_table_mapped += 4096;
    }
    process_initialize_slot(&processes[process_count]);
    ++process_count;
    return 1;
}

int process_abort(Process *process, unsigned int slot)
{
    if (process_release(process, slot))
    {
        process->state = TASK_UNUSED;
        return 1;
    }

    process->state = TASK_CLEANUP_ERROR;
    return 0;
}

void process_cleanup_terminated(void)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_DYING)
        {
            int resources_released = 1;

            if (processes[slot].window_handle != 0)
            {
                resources_released = video_destroy_window(
                    processes[slot].process_id,
                    processes[slot].window_handle);
                if (resources_released)
                {
                    processes[slot].window_handle = 0;
                }
            }

            if (resources_released &&
                process_release(&processes[slot], slot))
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
                            waiter->saved_stack[11] = processes[slot].exit_status;
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
                    "resource cleanup failed for pid",
                    processes[slot].process_id);
                terminal_write("process: failed to release task resources.\n");
                for (waiter_slot = 1;
                    waiter_slot < TASK_LIMIT;
                    ++waiter_slot)
                {
                    Process *waiter = &processes[waiter_slot];

                    if (waiter->state == TASK_WAITING &&
                        waiter->process_id == parent_id &&
                        waiter->wait_process_id == child_id)
                    {
                        waiter->saved_stack[11] = (unsigned int)-1;
                        waiter->wait_process_id = 0;
                        waiter->state = TASK_ACTIVE;
                        break;
                    }
                }
            }
        }
    }
}

unsigned int find_next_active_task(void)
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

void process_terminate(Process *process)
{
    unsigned int slot;

    if (process->state != TASK_ACTIVE && process->state != TASK_SLEEPING &&
        process->state != TASK_DESKTOP_WAIT)
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
    loopback_close_owner(process->process_id);
    desktop_server_exit(process->process_id);
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

void process_terminate_current(void)
{
    if (current_task != 0 &&
        processes[current_task].state == TASK_ACTIVE)
    {
        process_terminate(&processes[current_task]);
        current_task = find_next_active_task();
    }
}

void processes_initialize(void)
{
    process_count = 0;
    process_table_mapped = 0;
    if (!process_table_grow())
    {
        kernel_log(KERNEL_LOG_ERROR, "process", "process table unavailable");
        return;
    }

    process_pipe.active = 0;
    processes[0].state = TASK_ACTIVE;
    processes[0].process_id = 0;
    processes[0].name[0] = 'k';
    processes[0].name[1] = 'e';
    processes[0].name[2] = 'r';
    processes[0].name[3] = 'n';
    processes[0].name[4] = 'e';
    processes[0].name[5] = 'l';
    processes[0].name[6] = '\0';
    current_task = 0;
    kernel_log(KERNEL_LOG_INFO, "process", "scheduler initialized");
}

Process *process_find_active(unsigned int process_id)
{
    unsigned int index;

    for (index = 1; index < TASK_LIMIT; ++index)
    {
        if (processes[index].process_id == process_id &&
            processes[index].state == TASK_ACTIVE)
        {
            return &processes[index];
        }
    }
    return 0;
}

unsigned int __cdecl process_timer_tick(unsigned int *saved_stack)
{
    unsigned int slot;

    processes[current_task].saved_stack = saved_stack;
    __outbyte(0x20, 0x20);
    ++tick_count;
    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_SLEEPING &&
            (int)(tick_count - processes[slot].wake_tick) >= 0)
        {
            processes[slot].state = TASK_ACTIVE;
        }
    }
    process_cleanup_terminated();
    current_task = find_next_active_task();
    process_activate_current();
    return (unsigned int)processes[current_task].saved_stack;
}

unsigned int __cdecl process_handle_exception(unsigned int *frame)
{
    unsigned int vector = frame[12];

    if ((frame[15] & 3) != 3 || current_task == 0)
    {
        terminal_write("kernel exception ");
        terminal_write_unsigned(vector);
        terminal_write(" at eip ");
        terminal_write_unsigned(frame[14]);
        terminal_write("\n");
        for (;;)
        {
            __halt();
        }
    }

    processes[current_task].saved_stack = frame;
    terminal_write("process: exception ");
    terminal_write_unsigned(vector);
    terminal_write(" in pid ");
    terminal_write_unsigned(processes[current_task].process_id);
    terminal_write("; terminating.\n");
    process_terminate_current();
    process_activate_current();
    return (unsigned int)processes[current_task].saved_stack;
}
