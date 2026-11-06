#include <intrin.h>
#include "filesystem.h"
#include "kernel_log.h"
#include "process_internal.h"

int process_keyboard_try_read_scancode(
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

int process_pipe_write(
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

int process_pipe_read(
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

int process_stream_read(
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

int process_stream_write(
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

void process_pipe_close_endpoint(const Process *process)
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
    unsigned int focus_owner = video_active_owner_process_id();

    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *process = &processes[slot];
        unsigned char next;
        unsigned char next_input;
        char character;
        int focused;

        if (process->state != TASK_ACTIVE &&
            process->state != TASK_SLEEPING)
        {
            continue;
        }
        if (process->background)
        {
            continue;
        }

        /* Decode for everyone so modifier state stays correct, but only
           the process owning the top window receives the key. */
        focused = focus_owner != 0
            ? process->process_id == focus_owner
            : process->window_handle == 0;
        character = keyboard_decode_scancode(
            scancode,
            &process->keyboard_shift_pressed,
            &process->keyboard_caps_lock,
            &process->keyboard_extended);
        if (!focused)
        {
            continue;
        }
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
