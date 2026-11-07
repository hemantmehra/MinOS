#include "desktop.h"
#include "kernel_log.h"
#include "process_internal.h"

#define DESKTOP_QUEUE_SIZE 64

typedef struct
{
    unsigned int kind, x, y;
} MouseEvent;

static unsigned int server_id;
static MouseEvent events[DESKTOP_QUEUE_SIZE];
static unsigned int event_read, event_write;

void desktop_set_server(unsigned int pid)
{
    server_id = pid;
    event_read = event_write = 0;
}

unsigned int desktop_server_id(void)
{
    return server_id;
}

void desktop_server_exit(unsigned int pid)
{
    unsigned int slot;
    if (server_id != pid) return;
    server_id = 0;
    event_read = event_write = 0;
    for (slot = 1; slot < TASK_LIMIT; ++slot)
        if (processes[slot].state == TASK_DESKTOP_WAIT)
        {
            processes[slot].saved_stack[11] = 0;
            processes[slot].state = TASK_ACTIVE;
        }
}

void desktop_mouse_event(unsigned int x, unsigned int y, unsigned int kind)
{
    unsigned int next;
    if (!server_id) return;
    next = (event_write + 1) % DESKTOP_QUEUE_SIZE;
    if (kind == 2 && event_read != event_write)
    {
        unsigned int last = (event_write + DESKTOP_QUEUE_SIZE - 1) % DESKTOP_QUEUE_SIZE;
        if (events[last].kind == 2)
        {
            events[last].x = x;
            events[last].y = y;
            return;
        }
    }
    if (next == event_read)
    {
        kernel_log(KERNEL_LOG_WARNING, "desktop", "mouse event queue full");
        return;
    }
    events[event_write].kind = kind;
    events[event_write].x = x;
    events[event_write].y = y;
    event_write = next;
}

int desktop_next(unsigned int *event)
{
    unsigned int slot;
    for (slot = 1; slot < TASK_LIMIT; ++slot)
        if (processes[slot].state == TASK_DESKTOP_WAIT)
        {
            event[0] = 1;
            event[1] = processes[slot].process_id;
            event[2] = processes[slot].desktop_width <= 65535 &&
                processes[slot].desktop_height <= 65535
                ? processes[slot].desktop_width |
                    (processes[slot].desktop_height << 16) : 0;
            return 1;
        }
    if (event_read == event_write) return 0;
    event[0] = events[event_read].kind + 1;
    event[1] = events[event_read].x;
    event[2] = events[event_read].y;
    event_read = (event_read + 1) % DESKTOP_QUEUE_SIZE;
    return 1;
}

int desktop_create(unsigned int client_id, unsigned int approve)
{
    unsigned int slot;
    for (slot = 1; slot < TASK_LIMIT; ++slot)
    {
        Process *client = &processes[slot];
        if (client->process_id == client_id && client->state == TASK_DESKTOP_WAIT)
        {
            client->window_handle = approve ? video_create_window(client_id,
                client->desktop_title, client->desktop_width, client->desktop_height) : 0;
            client->saved_stack[11] = client->window_handle;
            client->state = TASK_ACTIVE;
            return 0;
        }
    }
    return -1;
}
