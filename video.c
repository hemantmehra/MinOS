#include <intrin.h>
#include "kernel.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 30
#define VGA_GRAPHICS_WIDTH 640
#define VGA_GRAPHICS_HEIGHT 480
#define VGA_GRAPHICS_BYTES_PER_ROW (VGA_GRAPHICS_WIDTH / 8)
#define VGA_TEXT_FONT_CHARACTERS 128
#define VGA_TEXT_FONT_HEIGHT 16

static unsigned char vga_text_font[
    VGA_TEXT_FONT_CHARACTERS * VGA_TEXT_FONT_HEIGHT];
static unsigned short terminal_position;
static unsigned short terminal_cells[VGA_WIDTH * VGA_HEIGHT];

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
    unsigned short position = vga_cursor_position();

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
        terminal_cells[position] =
            (unsigned short)(0x0F00 | (unsigned char)character);
        vga_render_character(position, (unsigned char)character, 0x0F);
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
}

void terminal_write(const char *text)
{
    while (*text != '\0')
    {
        terminal_write_character(*text);
        ++text;
    }
}

void terminal_clear(void)
{
    unsigned int index;

    for (index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index)
    {
        terminal_cells[index] = 0x0F20;
    }

    vga_fill_screen(0);
    vga_set_cursor_position(0);
}

static void vga_write_indexed_registers(
    unsigned short index_port,
    unsigned short data_port,
    const unsigned char *registers,
    unsigned int count)
{
    unsigned int index;

    for (index = 0; index < count; ++index)
    {
        __outbyte(index_port, (unsigned char)index);
        __outbyte(data_port, registers[index]);
    }
}

static void vga_write_attribute_registers(const unsigned char *registers)
{
    unsigned int index;

    for (index = 0; index < 21; ++index)
    {
        __inbyte(0x3DA);
        __outbyte(0x3C0, (unsigned char)index);
        __outbyte(0x3C0, registers[index]);
    }

    __inbyte(0x3DA);
    __outbyte(0x3C0, 0x20);
}

static void vga_set_graphics_mode(void)
{
    static const unsigned char graphics_sequencer[] =
        { 0x03, 0x01, 0x0F, 0x00, 0x06 };
    static const unsigned char graphics_crtc[] =
        {
            0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
            0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
            0xFF
        };
    static const unsigned char graphics_controller[] =
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF };
    static const unsigned char graphics_attributes[] =
        {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x01, 0x00, 0x0F, 0x00, 0x00
        };

    __outbyte(0x3C2, 0xE3);
    vga_write_indexed_registers(
        0x3C4, 0x3C5, graphics_sequencer, 5);
    __outbyte(0x3D4, 0x11);
    __outbyte(0x3D5, (unsigned char)(__inbyte(0x3D5) & 0x7F));
    vga_write_indexed_registers(0x3D4, 0x3D5, graphics_crtc, 25);
    vga_write_indexed_registers(
        0x3CE, 0x3CF, graphics_controller, 9);
    vga_write_attribute_registers(graphics_attributes);
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

static void vga_set_write_color(unsigned char color)
{
    __outbyte(0x3C4, 2);
    __outbyte(0x3C5, 0x0F);
    __outbyte(0x3CE, 1);
    __outbyte(0x3CF, 0x0F);
    __outbyte(0x3CE, 3);
    __outbyte(0x3CF, 0x00);
    __outbyte(0x3CE, 5);
    __outbyte(0x3CF, 0x00);
    __outbyte(0x3CE, 0);
    __outbyte(0x3CF, color);
}

static void vga_fill_screen(unsigned char color)
{
    volatile unsigned char *video_memory =
        (volatile unsigned char *)0xA0000;
    unsigned int index;

    vga_set_write_color(color);
    __outbyte(0x3CE, 8);
    __outbyte(0x3CF, 0xFF);
    for (index = 0;
        index < VGA_GRAPHICS_BYTES_PER_ROW * VGA_GRAPHICS_HEIGHT;
        ++index)
    {
        video_memory[index] = 0xFF;
    }
}

static void vga_write_masked_byte(
    volatile unsigned char *video_memory,
    unsigned int offset)
{
    unsigned char latches = video_memory[offset];

    (void)latches;
    video_memory[offset] = 0xFF;
}

static void vga_draw_glyph_pixels(
    volatile unsigned char *video_memory,
    unsigned int x,
    unsigned int y,
    unsigned char glyph,
    unsigned char draw_set_pixels)
{
    unsigned int column;

    __outbyte(0x3CE, 8);
    for (column = 0; column < 8; ++column)
    {
        unsigned char set_pixel =
            (glyph & (0x80 >> column)) != 0;
        if (set_pixel == draw_set_pixels)
        {
            unsigned int pixel_x = x + column;
            __outbyte(0x3CF, (unsigned char)(0x80 >> (pixel_x & 7)));
            vga_write_masked_byte(
                video_memory,
                y * VGA_GRAPHICS_BYTES_PER_ROW + pixel_x / 8);
        }
    }
}

static void vga_render_character(
    unsigned short position,
    unsigned char character,
    unsigned char attribute)
{
    volatile unsigned char *video_memory =
        (volatile unsigned char *)0xA0000;
    unsigned int row;
    unsigned char foreground = attribute & 0x0F;
    unsigned char background = attribute >> 4;
    unsigned int x = (position % VGA_WIDTH) * 8;
    unsigned int y = (position / VGA_WIDTH) * VGA_TEXT_FONT_HEIGHT;
    unsigned char glyphs[VGA_TEXT_FONT_HEIGHT];

    if (character >= VGA_TEXT_FONT_CHARACTERS ||
        (character == ' ' && background == 0))
    {
        return;
    }

    for (row = 0; row < VGA_TEXT_FONT_HEIGHT; ++row)
    {
        glyphs[row] = vga_text_font[
            character * VGA_TEXT_FONT_HEIGHT + row];
    }

    vga_set_write_color(background);
    for (row = 0; row < VGA_TEXT_FONT_HEIGHT; ++row)
    {
        vga_draw_glyph_pixels(video_memory, x, y + row, glyphs[row], 0);
    }

    vga_set_write_color(foreground);
    for (row = 0; row < VGA_TEXT_FONT_HEIGHT; ++row)
    {
        vga_draw_glyph_pixels(video_memory, x, y + row, glyphs[row], 1);
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
    int y)
{
    volatile unsigned char *video_memory =
        (volatile unsigned char *)0xA0000;
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

    __outbyte(0x3CE, 0);
    __outbyte(0x3CF, 4);
    __outbyte(0x3CE, 8);
    for (index = 0; index < 8; ++index)
    {
        int point_x = points[index][0];
        int point_y = points[index][1];

        __outbyte(0x3CF, (unsigned char)(0x80 >> (point_x & 7)));
        vga_write_masked_byte(
            video_memory,
            point_y * VGA_GRAPHICS_BYTES_PER_ROW + point_x / 8);
    }
}

void video_draw_circle(void)
{
    int x = 140;
    int y = 0;
    int error = 1 - x;

    vga_set_graphics_mode();
    vga_fill_screen(15);

    while (x >= y)
    {
        graphics_plot_circle_point(320, 240, x, y);
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
    vga_set_graphics_mode();
    vga_initialize_palette();
    terminal_clear();
}
