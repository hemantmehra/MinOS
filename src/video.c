#include <intrin.h>
#include "kernel.h"

#define VGA_WIDTH 160
#define VGA_HEIGHT 45
#define VGA_GRAPHICS_WIDTH 1280
#define VGA_GRAPHICS_HEIGHT 720
#define VGA_FRAMEBUFFER_SIZE \
    (VGA_GRAPHICS_WIDTH * VGA_GRAPHICS_HEIGHT)
#define VGA_FRAMEBUFFER_VIRTUAL 0xD0000000
#define VGA_FRAMEBUFFER_PAGES \
    (VGA_FRAMEBUFFER_SIZE / 4096)
#define VGA_VBE_INDEX_PORT 0x1CE
#define VGA_VBE_DATA_PORT 0x1CF
#define VGA_VBE_INDEX_ID 0
#define VGA_VBE_INDEX_XRES 1
#define VGA_VBE_INDEX_YRES 2
#define VGA_VBE_INDEX_BPP 3
#define VGA_VBE_INDEX_ENABLE 4
#define VGA_VBE_INDEX_VIRTUAL_WIDTH 6
#define VGA_VBE_INDEX_X_OFFSET 8
#define VGA_VBE_INDEX_Y_OFFSET 9
#define VGA_VBE_ENABLE 0x01
#define VGA_VBE_LINEAR_FRAMEBUFFER 0x40
#define VGA_TEXT_FONT_CHARACTERS 128
#define VGA_TEXT_FONT_HEIGHT 16

static unsigned char vga_text_font[
    VGA_TEXT_FONT_CHARACTERS * VGA_TEXT_FONT_HEIGHT];
static unsigned short terminal_position;
static unsigned short terminal_cells[VGA_WIDTH * VGA_HEIGHT];
static unsigned char terminal_attribute = 0x0F;
static volatile unsigned char *vga_framebuffer;

static void vga_render_character(
    unsigned short position,
    unsigned char character,
    unsigned char attribute);
static void vga_fill_screen(unsigned char color);
static void vga_render_terminal(void);

static unsigned short vga_cursor_position(void)
{
    return terminal_position;
}

static void vga_set_cursor_position(unsigned short position)
{
    terminal_position = position;
}

static void vga_scroll(void)
{
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * (VGA_HEIGHT - 1); ++index)
    {
        terminal_cells[index] = terminal_cells[index + VGA_WIDTH];
    }

    for (; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        terminal_cells[index] = 0x0F20;
    }

    vga_render_terminal();
}

void terminal_write_character(char character)
{
    unsigned int flags;
    unsigned short position;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    position = vga_cursor_position();

    if (character == '\r')
    {
        position -= position % VGA_WIDTH;
    }
    else if (character == '\n')
    {
        position += VGA_WIDTH - position % VGA_WIDTH;
    }
    else if (character == '\b')
    {
        if (position != 0)
        {
            --position;
            terminal_cells[position] = 0x0F20;
            vga_render_character(position, ' ', 0x0F);
        }
    }
    else
    {
        terminal_cells[position] = (unsigned short)(
            ((unsigned short)terminal_attribute << 8) |
            (unsigned char)character);
        vga_render_character(
            position, (unsigned char)character, terminal_attribute);
        ++position;
        __outbyte(0xE9, (unsigned char)character);
    }

    if (position >= VGA_WIDTH * VGA_HEIGHT)
    {
        vga_scroll();
        position -= VGA_WIDTH;
    }

    vga_set_cursor_position(position);

    if (character == '\n')
    {
        __outbyte(0xE9, '\n');
    }
    else if (character == '\b')
    {
        __outbyte(0xE9, '\b');
    }

    __asm
    {
        push flags
        popfd
    }
}

void terminal_write(const char *text)
{
    while (*text != '\0')
    {
        terminal_write_character(*text);
        ++text;
    }
}

void terminal_write_prompt(const char *text)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    terminal_attribute = 0x09;
    terminal_write(text);
    terminal_attribute = 0x0F;

    __asm
    {
        push flags
        popfd
    }
}

void terminal_clear(void)
{
    unsigned int flags;
    unsigned int index;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    for (index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        terminal_cells[index] = 0x0F20;
    }

    vga_fill_screen(0);
    vga_set_cursor_position(0);

    __asm
    {
        push flags
        popfd
    }
}

static unsigned int pci_read_config(
    unsigned int bus,
    unsigned int device,
    unsigned int function,
    unsigned int offset)
{
    unsigned int address =
        0x80000000 |
        (bus << 16) |
        (device << 11) |
        (function << 8) |
        (offset & 0xFC);

    __outdword(0xCF8, address);
    return __indword(0xCFC);
}

static unsigned int vga_find_framebuffer(void)
{
    unsigned int device;

    for (device = 0; device < 32; ++device)
    {
        unsigned int vendor_and_device =
            pci_read_config(0, device, 0, 0);
        unsigned int class_and_subclass =
            pci_read_config(0, device, 0, 8);

        if ((vendor_and_device & 0xFFFF) == 0xFFFF ||
            (class_and_subclass >> 24) != 0x03)
        {
            continue;
        }

        {
            unsigned int bar = pci_read_config(0, device, 0, 0x10);
            unsigned int framebuffer = bar & 0xFFFFFFF0;

            if ((bar & 1) == 0 && framebuffer != 0)
            {
                return framebuffer;
            }
        }
    }

    return 0;
}

static void vbe_write_register(
    unsigned short index,
    unsigned short value)
{
    __outword(VGA_VBE_INDEX_PORT, index);
    __outword(VGA_VBE_DATA_PORT, value);
}

static unsigned short vbe_read_register(unsigned short index)
{
    __outword(VGA_VBE_INDEX_PORT, index);
    return __inword(VGA_VBE_DATA_PORT);
}

static int vga_set_graphics_mode(void)
{
    unsigned int framebuffer = vga_find_framebuffer();
    unsigned int page;

    if (framebuffer == 0 || vbe_read_register(VGA_VBE_INDEX_ID) < 0xB0C0)
    {
        return 0;
    }

    vbe_write_register(VGA_VBE_INDEX_ENABLE, 0);
    vbe_write_register(VGA_VBE_INDEX_XRES, VGA_GRAPHICS_WIDTH);
    vbe_write_register(VGA_VBE_INDEX_YRES, VGA_GRAPHICS_HEIGHT);
    vbe_write_register(VGA_VBE_INDEX_BPP, 8);
    vbe_write_register(VGA_VBE_INDEX_VIRTUAL_WIDTH, VGA_GRAPHICS_WIDTH);
    vbe_write_register(VGA_VBE_INDEX_X_OFFSET, 0);
    vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
    vbe_write_register(
        VGA_VBE_INDEX_ENABLE,
        VGA_VBE_ENABLE | VGA_VBE_LINEAR_FRAMEBUFFER);

    for (page = 0; page < VGA_FRAMEBUFFER_PAGES; ++page)
    {
        unsigned int physical_address = framebuffer + page * 4096;

        if (!memory_map_page(
            VGA_FRAMEBUFFER_VIRTUAL + page * 4096,
            physical_address,
            0x02))
        {
            while (page != 0)
            {
                unsigned int unmapped_address;
                --page;
                memory_unmap_page(
                    VGA_FRAMEBUFFER_VIRTUAL + page * 4096,
                    &unmapped_address);
            }
            return 0;
        }
    }

    vga_framebuffer =
        (volatile unsigned char *)VGA_FRAMEBUFFER_VIRTUAL;
    return 1;
}

static void vga_save_text_font(void)
{
    volatile const unsigned char *font_memory =
        (volatile const unsigned char *)0x5000;
    unsigned int index;

    for (index = 0;
        index < VGA_TEXT_FONT_CHARACTERS * VGA_TEXT_FONT_HEIGHT;
        ++index)
    {
        vga_text_font[index] = font_memory[index];
    }
}

static void vga_set_palette_color(
    unsigned char index,
    unsigned char red,
    unsigned char green,
    unsigned char blue)
{
    __outbyte(0x3C8, index);
    __outbyte(0x3C9, red);
    __outbyte(0x3C9, green);
    __outbyte(0x3C9, blue);
}

static void vga_initialize_palette(void)
{
    static const unsigned char colors[16][3] =
        {
            { 0, 0, 0 }, { 0, 0, 42 }, { 0, 42, 0 }, { 0, 42, 42 },
            { 42, 0, 0 }, { 42, 0, 42 }, { 42, 21, 0 }, { 42, 42, 42 },
            { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 }, { 21, 63, 63 },
            { 63, 21, 21 }, { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 }
        };
    unsigned int index;

    for (index = 0; index < 16; ++index)
    {
        vga_set_palette_color(
            (unsigned char)index,
            colors[index][0],
            colors[index][1],
            colors[index][2]);
    }
}

static void vga_fill_screen(unsigned char color)
{
    unsigned int index;

    for (index = 0; index < VGA_FRAMEBUFFER_SIZE; ++index)
    {
        vga_framebuffer[index] = color;
    }
}

static void vga_render_character(
    unsigned short position,
    unsigned char character,
    unsigned char attribute)
{
    unsigned int row;
    unsigned char foreground = attribute & 0x0F;
    unsigned char background = attribute >> 4;
    unsigned int x = (position % VGA_WIDTH) * 8;
    unsigned int y = (position / VGA_WIDTH) * VGA_TEXT_FONT_HEIGHT;
    unsigned int column;

    if (character >= VGA_TEXT_FONT_CHARACTERS)
    {
        return;
    }

    for (row = 0; row < VGA_TEXT_FONT_HEIGHT; ++row)
    {
        unsigned char glyph = vga_text_font[
            character * VGA_TEXT_FONT_HEIGHT + row];

        for (column = 0; column < 8; ++column)
        {
            unsigned char color =
                (glyph & (0x80 >> column)) != 0
                ? foreground
                : background;
            vga_framebuffer[
                (y + row) * VGA_GRAPHICS_WIDTH + x + column] = color;
        }
    }
}

static void vga_render_terminal(void)
{
    unsigned int position;

    vga_fill_screen(0);
    for (position = 0; position < VGA_WIDTH * VGA_HEIGHT; ++position)
    {
        unsigned short cell = terminal_cells[position];
        unsigned char character = (unsigned char)cell;

        if (character != ' ')
        {
            vga_render_character(
                (unsigned short)position,
                character,
                (unsigned char)(cell >> 8));
        }
    }
}

static void graphics_plot_circle_point(
    int center_x,
    int center_y,
    int x,
    int y,
    unsigned char color)
{
    int points[8][2] =
        {
            { center_x + x, center_y + y },
            { center_x - x, center_y + y },
            { center_x + x, center_y - y },
            { center_x - x, center_y - y },
            { center_x + y, center_y + x },
            { center_x - y, center_y + x },
            { center_x + y, center_y - x },
            { center_x - y, center_y - x }
        };
    unsigned int index;

    for (index = 0; index < 8; ++index)
    {
        int point_x = points[index][0];
        int point_y = points[index][1];

        vga_framebuffer[
            point_y * VGA_GRAPHICS_WIDTH + point_x] = color;
    }
}

static void graphics_draw_circle(
    int center_x,
    int center_y,
    int radius,
    unsigned char color)
{
    int x = radius;
    int y = 0;
    int error = 1 - x;

    while (x >= y)
    {
        graphics_plot_circle_point(
            center_x, center_y, x, y, color);
        ++y;
        if (error < 0)
        {
            error += 2 * y + 1;
        }
        else
        {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

void video_start_graphics(void)
{
    video_clear_graphics(15);
}

void video_clear_graphics(unsigned char color)
{
    vga_fill_screen(color);
}

void video_draw_pixel(
    unsigned int x,
    unsigned int y,
    unsigned char color)
{
    if (x < VGA_GRAPHICS_WIDTH && y < VGA_GRAPHICS_HEIGHT)
    {
        vga_framebuffer[y * VGA_GRAPHICS_WIDTH + x] = color;
    }
}

void video_end_graphics(void)
{
    vga_render_terminal();
}

void video_terminal_restore(void)
{
    vga_render_terminal();
}

void terminal_write_unsigned(unsigned int value)
{
    char digits[10];
    unsigned int length = 0;

    do
    {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (length != 0)
    {
        terminal_write_character(digits[--length]);
    }
}

void video_initialize(void)
{
    vga_save_text_font();
    if (!vga_set_graphics_mode())
    {
        const char *error =
            "video: VBE 1280x720 framebuffer setup failed.\n";

        while (*error != '\0')
        {
            __outbyte(0xE9, (unsigned char)*error);
            ++error;
        }
        for (;;)
        {
            __asm hlt
        }
    }
    vga_initialize_palette();
    terminal_clear();
}
