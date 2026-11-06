#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"

int process_is_running(unsigned int process_id)
{
    unsigned int flags = disable_interrupts();
    unsigned int slot;
    int running = 0;
    for (slot = 1; process_id != 0 && slot < TASK_LIMIT; ++slot)
        if (processes[slot].process_id == process_id &&
            (processes[slot].state == TASK_ACTIVE ||
             processes[slot].state == TASK_SLEEPING ||
             processes[slot].state == TASK_WAITING ||
             processes[slot].state == TASK_LOADING))
        {
            running = 1;
            break;
        }
    restore_interrupts(flags);
    return running;
}

int process_has_active_process(void)
{
    unsigned int slot;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if (processes[slot].state == TASK_ACTIVE ||
            processes[slot].state == TASK_SLEEPING)
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
                processes[slot].state == TASK_SLEEPING ||
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

        if ((process->state == TASK_ACTIVE ||
                process->state == TASK_SLEEPING) &&
            process->process_id != caller_process_id &&
            process_name_matches(process->name, name))
        {
            process_terminate(process);
            return 1;
        }
    }

    return 0;
}

int process_kill_by_id(unsigned int process_id)
{
    unsigned int flags = disable_interrupts();
    unsigned int slot;
    int killed = 0;

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        if ((processes[slot].state == TASK_ACTIVE ||
                processes[slot].state == TASK_SLEEPING) &&
            processes[slot].process_id == process_id)
        {
            process_terminate(&processes[slot]);
            killed = 1;
            break;
        }
    }
    restore_interrupts(flags);
    return killed;
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
        case TASK_SLEEPING:
            terminal_write("SLEEPING ");
            break;
        case TASK_WAITING:
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
