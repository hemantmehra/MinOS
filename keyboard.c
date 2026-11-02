#include <intrin.h>
#include "kernel.h"

#define KEYBOARD_QUEUE_SIZE 64
#define KEYBOARD_QUEUE_MASK (KEYBOARD_QUEUE_SIZE - 1)

static volatile unsigned char keyboard_queue[KEYBOARD_QUEUE_SIZE];
static volatile unsigned char keyboard_queue_read;
static volatile unsigned char keyboard_queue_write;

static void keyboard_enqueue_scancode(unsigned char scancode)
{
    unsigned char next =
        (unsigned char)((keyboard_queue_write + 1) & KEYBOARD_QUEUE_MASK);

    if (next != keyboard_queue_read)
    {
        keyboard_queue[keyboard_queue_write] = scancode;
        keyboard_queue_write = next;
    }
}

void keyboard_handle_irq(void)
{
    keyboard_enqueue_scancode(__inbyte(0x60));
    __outbyte(0x20, 0x20);
}

static char translate_scancode(
    unsigned char scancode,
    unsigned char shift_pressed,
    unsigned char caps_lock)
{
    char character = '\0';

    if (scancode >= 0x02 && scancode <= 0x0B)
    {
        const char *digits = shift_pressed ? "!@#$%^&*()" : "1234567890";
        character = digits[scancode - 0x02];
    }
    else if (scancode >= 0x10 && scancode <= 0x19)
    {
        character = "qwertyuiop"[scancode - 0x10];
    }
    else if (scancode >= 0x1E && scancode <= 0x26)
    {
        character = "asdfghjkl"[scancode - 0x1E];
    }
    else if (scancode >= 0x2C && scancode <= 0x32)
    {
        character = "zxcvbnm"[scancode - 0x2C];
    }
    else if (scancode == 0x39)
    {
        character = ' ';
    }
    else
    {
        switch (scancode)
        {
        case 0x0C: character = shift_pressed ? '_' : '-'; break;
        case 0x0D: character = shift_pressed ? '+' : '='; break;
        case 0x1A: character = shift_pressed ? '{' : '['; break;
        case 0x1B: character = shift_pressed ? '}' : ']'; break;
        case 0x27: character = shift_pressed ? ':' : ';'; break;
        case 0x28: character = shift_pressed ? '"' : '\''; break;
        case 0x29: character = shift_pressed ? '~' : '`'; break;
        case 0x2B: character = shift_pressed ? '|' : '\\'; break;
        case 0x33: character = shift_pressed ? '<' : ','; break;
        case 0x34: character = shift_pressed ? '>' : '.'; break;
        case 0x35: character = shift_pressed ? '?' : '/'; break;
        default: break;
        }
    }

    if (character >= 'a' && character <= 'z' &&
        (shift_pressed != caps_lock))
    {
        character = (char)(character - 'a' + 'A');
    }

    return character;
}

char keyboard_read_character(
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended)
{
    for (;;)
    {
        unsigned char scancode;

        while (keyboard_queue_read == keyboard_queue_write)
        {
        }

        scancode = keyboard_queue[keyboard_queue_read];
        keyboard_queue_read = (unsigned char)(
            (keyboard_queue_read + 1) & KEYBOARD_QUEUE_MASK);

        if (*extended)
        {
            *extended = 0;
            continue;
        }

        if (scancode == 0xE0)
        {
            *extended = 1;
            continue;
        }

        if (scancode == 0x2A || scancode == 0x36)
        {
            *shift_pressed = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6)
        {
            *shift_pressed = 0;
            continue;
        }

        if ((scancode & 0x80) != 0)
        {
            continue;
        }

        if (scancode == 0x3A)
        {
            *caps_lock = (unsigned char)!*caps_lock;
            continue;
        }

        if (scancode == 0x1C)
        {
            return '\n';
        }

        if (scancode == 0x01)
        {
            return '\x1B';
        }

        if (scancode == 0x0E)
        {
            return '\b';
        }

        return translate_scancode(scancode, *shift_pressed, *caps_lock);
    }
}
