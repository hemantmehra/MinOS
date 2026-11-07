#include <intrin.h>
#include "kernel.h"
#include "desktop.h"

#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_DATA_PORT 0x60
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02
#define PS2_STATUS_AUXILIARY_DATA 0x20
#define PS2_WAIT_LIMIT 100000
#define MOUSE_PACKET_SIZE 3
#define VGA_SCREEN_WIDTH 1280
#define VGA_SCREEN_HEIGHT 720

static unsigned char mouse_packet[MOUSE_PACKET_SIZE];
static unsigned char mouse_packet_index;
static unsigned char mouse_left_button_pressed;
static unsigned int mouse_x = VGA_SCREEN_WIDTH / 2;
static unsigned int mouse_y = VGA_SCREEN_HEIGHT / 2;

static int ps2_wait_input_empty(void)
{
    unsigned int attempt;

    for (attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt)
    {
        if ((__inbyte(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int ps2_wait_output_full(void)
{
    unsigned int attempt;

    for (attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt)
    {
        if ((__inbyte(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0)
        {
            return 1;
        }
    }
    return 0;
}

static int mouse_send_command(unsigned char command)
{
    unsigned int attempt;

    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    __outbyte(PS2_COMMAND_PORT, 0xD4);
    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    __outbyte(PS2_DATA_PORT, command);

    for (attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt)
    {
        unsigned char status = __inbyte(PS2_STATUS_PORT);

        if ((status & PS2_STATUS_OUTPUT_FULL) != 0)
        {
            unsigned char response = __inbyte(PS2_DATA_PORT);

            if ((status & PS2_STATUS_AUXILIARY_DATA) != 0)
            {
                return response == 0xFA;
            }
        }
    }
    return 0;
}

int mouse_initialize(void)
{
    unsigned char controller_command;

    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    __outbyte(PS2_COMMAND_PORT, 0xA8);
    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    __outbyte(PS2_COMMAND_PORT, 0x20);
    if (!ps2_wait_output_full())
    {
        return 0;
    }
    controller_command = __inbyte(PS2_DATA_PORT);

    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    __outbyte(PS2_COMMAND_PORT, 0x60);
    if (!ps2_wait_input_empty())
    {
        return 0;
    }
    controller_command = (unsigned char)(
        (controller_command | 0x02) & ~0x20);
    __outbyte(PS2_DATA_PORT, controller_command);

    if (!mouse_send_command(0xF6) || !mouse_send_command(0xF4))
    {
        return 0;
    }

    mouse_packet_index = 0;
    video_mouse_move(mouse_x, mouse_y, 0);
    return 1;
}

void mouse_handle_irq(void)
{
    unsigned char status = __inbyte(PS2_STATUS_PORT);

    if ((status & PS2_STATUS_OUTPUT_FULL) != 0 &&
        (status & PS2_STATUS_AUXILIARY_DATA) != 0)
    {
        unsigned char value = __inbyte(PS2_DATA_PORT);

        if (mouse_packet_index == 0 && (value & 0x08) == 0)
        {
            __outbyte(0xA0, 0x20);
            __outbyte(0x20, 0x20);
            return;
        }

        mouse_packet[mouse_packet_index++] = value;
        if (mouse_packet_index == MOUSE_PACKET_SIZE)
        {
            unsigned char buttons = mouse_packet[0];
            int delta_x = (signed char)mouse_packet[1];
            int delta_y = (signed char)mouse_packet[2];
            int next_x;
            int next_y;

            mouse_packet_index = 0;
            next_x = (int)mouse_x + delta_x;
            next_y = (int)mouse_y - delta_y;
            if (next_x < 0)
            {
                next_x = 0;
            }
            else if (next_x >= VGA_SCREEN_WIDTH)
            {
                next_x = VGA_SCREEN_WIDTH - 1;
            }
            if (next_y < 0)
            {
                next_y = 0;
            }
            else if (next_y >= VGA_SCREEN_HEIGHT)
            {
                next_y = VGA_SCREEN_HEIGHT - 1;
            }
            mouse_x = (unsigned int)next_x;
            mouse_y = (unsigned int)next_y;
            video_mouse_move(mouse_x, mouse_y, buttons & 0x01);
            if ((buttons & 0x01) != 0 && !mouse_left_button_pressed)
            {
                desktop_mouse_event(mouse_x, mouse_y, 1);
            }
            else if ((buttons & 0x01) != 0)
                desktop_mouse_event(mouse_x, mouse_y, 2);
            else if (mouse_left_button_pressed)
                desktop_mouse_event(mouse_x, mouse_y, 3);
            mouse_left_button_pressed = buttons & 0x01;
        }
    }

    __outbyte(0xA0, 0x20);
    __outbyte(0x20, 0x20);
}
