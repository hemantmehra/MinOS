#include <intrin.h>
#include "kernel.h"
#include "kernel_log.h"

#define VGA_WIDTH 144
#define VGA_HEIGHT 36
#define VGA_GRAPHICS_WIDTH 1280
#define VGA_GRAPHICS_HEIGHT 720
#define SHELL_WINDOW_X 48
#define SHELL_WINDOW_Y 40
#define SHELL_WINDOW_WIDTH 1184
#define SHELL_WINDOW_HEIGHT 640
#define SHELL_TITLE_HEIGHT 32
#define SHELL_CONTENT_X 64
#define SHELL_CONTENT_Y 88
#define DESKTOP_BACKGROUND_COLOR 7
#define SHELL_WINDOW_BORDER_COLOR 15
#define SHELL_TITLE_COLOR 3
#define SHELL_CONTENT_COLOR 0
#define SHELL_CLIENT_WIDTH 1152
#define SHELL_CLIENT_HEIGHT 576
#define GRAPHICS_WINDOW_LIMIT 8
#define GRAPHICS_WINDOW_TITLE_LENGTH 32
#define GRAPHICS_WINDOW_MAX_WIDTH 640
#define GRAPHICS_WINDOW_MAX_HEIGHT 480
#define GRAPHICS_WINDOW_BUFFER_SIZE \
    (GRAPHICS_WINDOW_MAX_WIDTH * GRAPHICS_WINDOW_MAX_HEIGHT)
#define GRAPHICS_WINDOW_PAGE_COUNT \
    ((GRAPHICS_WINDOW_BUFFER_SIZE * 2 + 4095) / 4096)
#define GRAPHICS_WINDOW_VIRTUAL_BASE 0xE0000000
#define GRAPHICS_WINDOW_VIRTUAL_STRIDE 0x00100000
#define MOUSE_POINTER_WIDTH 12
#define MOUSE_POINTER_HEIGHT 18
#define MOUSE_POINTER_STORAGE_SIZE \
    (MOUSE_POINTER_WIDTH * MOUSE_POINTER_HEIGHT)
#define VGA_FRAMEBUFFER_SIZE \
    (VGA_GRAPHICS_WIDTH * VGA_GRAPHICS_HEIGHT)
#define VGA_DOUBLE_BUFFER_SIZE (VGA_FRAMEBUFFER_SIZE * 2)
#define VGA_FRAMEBUFFER_VIRTUAL 0xD0000000
#define VGA_FRAMEBUFFER_PAGES \
    (VGA_DOUBLE_BUFFER_SIZE / 4096)
#define VGA_VBE_INDEX_PORT 0x1CE
#define VGA_VBE_DATA_PORT 0x1CF
#define VGA_VBE_INDEX_ID 0
#define VGA_VBE_INDEX_XRES 1
#define VGA_VBE_INDEX_YRES 2
#define VGA_VBE_INDEX_BPP 3
#define VGA_VBE_INDEX_ENABLE 4
#define VGA_VBE_INDEX_VIRTUAL_WIDTH 6
#define VGA_VBE_INDEX_VIRTUAL_HEIGHT 7
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
static volatile unsigned char *vga_back_buffer;
static unsigned int vga_back_buffer_index;

typedef struct
{
    unsigned int owner_process_id;
    unsigned int width;
    unsigned int height;
    unsigned int x;
    unsigned int y;
    unsigned int back_buffer_index;
    unsigned int page_count;
    unsigned int physical_pages[GRAPHICS_WINDOW_PAGE_COUNT];
    unsigned char page_mapped[GRAPHICS_WINDOW_PAGE_COUNT];
    volatile unsigned char *buffers[2];
    unsigned char visible;
    char title[GRAPHICS_WINDOW_TITLE_LENGTH];
} GraphicsWindow;

static GraphicsWindow graphics_windows[GRAPHICS_WINDOW_LIMIT];
static unsigned char shell_graphics_active;
static unsigned int mouse_pointer_x = VGA_GRAPHICS_WIDTH / 2;
static unsigned int mouse_pointer_y = VGA_GRAPHICS_HEIGHT / 2;
static unsigned char mouse_pointer_drawn[2];
static unsigned char mouse_pointer_background[2][MOUSE_POINTER_STORAGE_SIZE];

static void vga_fill_rectangle_to(
    volatile unsigned char *target,
    unsigned int x,
    unsigned int y,
    unsigned int width,
    unsigned int height,
    unsigned char color);
static void vga_render_glyph_to(
    volatile unsigned char *target,
    unsigned int x,
    unsigned int y,
    unsigned char character,
    unsigned char foreground,
    unsigned char background);
static void vga_render_terminal(void);
static void vga_render_terminal_to(volatile unsigned char *target);
static void vga_render_desktop_to(volatile unsigned char *target);
static void vga_render_windows_to(volatile unsigned char *target);
static void vga_render_mouse_pointer_to(volatile unsigned char *target);
static void vga_restore_mouse_pointer(unsigned int page);

static int graphics_window_lookup(
    unsigned int owner_process_id,
    unsigned int window_handle,
    GraphicsWindow **window)
{
    GraphicsWindow *candidate;

    if (window_handle == 0 || window_handle > GRAPHICS_WINDOW_LIMIT)
    {
        return 0;
    }

    candidate = &graphics_windows[window_handle - 1];
    if (candidate->owner_process_id != owner_process_id ||
        candidate->page_count == 0)
    {
        return 0;
    }

    *window = candidate;
    return 1;
}

static void graphics_fill_buffer(
    volatile unsigned char *buffer,
    unsigned int width,
    unsigned int height,
    unsigned char color)
{
    unsigned int index;

    for (index = 0; index < width * height; ++index)
    {
        buffer[index] = color;
    }
}

static void vga_render_shell_graphics_chrome_to(
    volatile unsigned char *target)
{
    const char *title = "MinOS Shell (always open)";
    unsigned int title_x = SHELL_WINDOW_X + 16;
    unsigned int title_y = SHELL_WINDOW_Y + 8;

    vga_fill_rectangle_to(
        target,
        0,
        0,
        VGA_GRAPHICS_WIDTH,
        SHELL_WINDOW_Y,
        DESKTOP_BACKGROUND_COLOR);
    vga_fill_rectangle_to(
        target,
        0,
        SHELL_WINDOW_Y,
        SHELL_WINDOW_X,
        SHELL_WINDOW_HEIGHT,
        DESKTOP_BACKGROUND_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + SHELL_WINDOW_WIDTH,
        SHELL_WINDOW_Y,
        VGA_GRAPHICS_WIDTH - SHELL_WINDOW_X - SHELL_WINDOW_WIDTH,
        SHELL_WINDOW_HEIGHT,
        DESKTOP_BACKGROUND_COLOR);
    vga_fill_rectangle_to(
        target,
        0,
        SHELL_WINDOW_Y + SHELL_WINDOW_HEIGHT,
        VGA_GRAPHICS_WIDTH,
        VGA_GRAPHICS_HEIGHT -
            SHELL_WINDOW_Y - SHELL_WINDOW_HEIGHT,
        DESKTOP_BACKGROUND_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X,
        SHELL_WINDOW_Y,
        SHELL_WINDOW_WIDTH,
        1,
        SHELL_WINDOW_BORDER_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X,
        SHELL_WINDOW_Y + 1,
        1,
        SHELL_WINDOW_HEIGHT - 2,
        SHELL_WINDOW_BORDER_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + SHELL_WINDOW_WIDTH - 1,
        SHELL_WINDOW_Y + 1,
        1,
        SHELL_WINDOW_HEIGHT - 2,
        SHELL_WINDOW_BORDER_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X,
        SHELL_WINDOW_Y + SHELL_WINDOW_HEIGHT - 1,
        SHELL_WINDOW_WIDTH,
        1,
        SHELL_WINDOW_BORDER_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_WINDOW_Y + 1,
        SHELL_WINDOW_WIDTH - 2,
        SHELL_TITLE_HEIGHT,
        SHELL_TITLE_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_WINDOW_Y + SHELL_TITLE_HEIGHT + 1,
        SHELL_WINDOW_WIDTH - 2,
        SHELL_CONTENT_Y - SHELL_WINDOW_Y - SHELL_TITLE_HEIGHT - 1,
        SHELL_CONTENT_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_CONTENT_Y,
        SHELL_CONTENT_X - SHELL_WINDOW_X - 1,
        SHELL_CLIENT_HEIGHT,
        SHELL_CONTENT_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_CONTENT_X + SHELL_CLIENT_WIDTH,
        SHELL_CONTENT_Y,
        SHELL_WINDOW_X + SHELL_WINDOW_WIDTH -
            1 - SHELL_CONTENT_X - SHELL_CLIENT_WIDTH,
        SHELL_CLIENT_HEIGHT,
        SHELL_CONTENT_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_CONTENT_Y + SHELL_CLIENT_HEIGHT,
        SHELL_WINDOW_WIDTH - 2,
        SHELL_WINDOW_Y + SHELL_WINDOW_HEIGHT - 1 -
            SHELL_CONTENT_Y - SHELL_CLIENT_HEIGHT,
        SHELL_CONTENT_COLOR);
    while (*title != '\0')
    {
        vga_render_glyph_to(
            target,
            title_x,
            title_y,
            (unsigned char)*title,
            15,
            SHELL_TITLE_COLOR);
        title_x += 8;
        ++title;
    }
}

static void vga_render_character(
    unsigned short position,
    unsigned char character,
    unsigned char attribute);

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
    }

    if (position >= VGA_WIDTH * VGA_HEIGHT)
    {
        vga_scroll();
        position -= VGA_WIDTH;
    }

    vga_set_cursor_position(position);

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

    vga_render_terminal();
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
    vbe_write_register(
        VGA_VBE_INDEX_VIRTUAL_HEIGHT, VGA_GRAPHICS_HEIGHT * 2);
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
    vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
    vga_back_buffer_index = 1;
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

static void vga_fill_rectangle_to(
    volatile unsigned char *target,
    unsigned int x,
    unsigned int y,
    unsigned int width,
    unsigned int height,
    unsigned char color)
{
    unsigned int row;

    for (row = 0; row < height; ++row)
    {
        unsigned int column;
        volatile unsigned char *destination =
            target + (y + row) * VGA_GRAPHICS_WIDTH + x;

        for (column = 0; column < width; ++column)
        {
            destination[column] = color;
        }
    }
}

static void vga_render_glyph_to(
    volatile unsigned char *target,
    unsigned int x,
    unsigned int y,
    unsigned char character,
    unsigned char foreground,
    unsigned char background)
{
    unsigned int row;

    if (character >= VGA_TEXT_FONT_CHARACTERS)
    {
        return;
    }

    for (row = 0; row < VGA_TEXT_FONT_HEIGHT; ++row)
    {
        unsigned char glyph = vga_text_font[
            character * VGA_TEXT_FONT_HEIGHT + row];
        unsigned int column;

        for (column = 0; column < 8; ++column)
        {
            target[
                (y + row) * VGA_GRAPHICS_WIDTH + x + column] =
                (glyph & (0x80 >> column)) != 0
                ? foreground
                : background;
        }
    }
}

static void vga_render_glyph(
    unsigned int x,
    unsigned int y,
    unsigned char character,
    unsigned char foreground,
    unsigned char background)
{
    vga_render_glyph_to(
        vga_framebuffer, x, y, character, foreground, background);
}

static void vga_render_character(
    unsigned short position,
    unsigned char character,
    unsigned char attribute)
{
    unsigned char foreground = attribute & 0x0F;
    unsigned char background = attribute >> 4;
    unsigned int x = SHELL_CONTENT_X + (position % VGA_WIDTH) * 8;
    unsigned int y =
        SHELL_CONTENT_Y + (position / VGA_WIDTH) * VGA_TEXT_FONT_HEIGHT;

    vga_render_glyph(
        x, y, character, foreground, background);
}

static void vga_render_shell_chrome_to(volatile unsigned char *target)
{
    const char *title = "MinOS Shell (always open)";
    unsigned int title_x = SHELL_WINDOW_X + 16;
    unsigned int title_y = SHELL_WINDOW_Y + 8;

    vga_fill_rectangle_to(
        target,
        0,
        0,
        VGA_GRAPHICS_WIDTH,
        VGA_GRAPHICS_HEIGHT,
        DESKTOP_BACKGROUND_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X,
        SHELL_WINDOW_Y,
        SHELL_WINDOW_WIDTH,
        SHELL_WINDOW_HEIGHT,
        SHELL_WINDOW_BORDER_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_WINDOW_Y + 1,
        SHELL_WINDOW_WIDTH - 2,
        SHELL_WINDOW_HEIGHT - 2,
        SHELL_CONTENT_COLOR);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 1,
        SHELL_WINDOW_Y + 1,
        SHELL_WINDOW_WIDTH - 2,
        SHELL_TITLE_HEIGHT,
        SHELL_TITLE_COLOR);
    while (*title != '\0')
    {
        vga_render_glyph_to(
            target,
            title_x,
            title_y,
            (unsigned char)*title,
            15,
            SHELL_TITLE_COLOR);
        title_x += 8;
        ++title;
    }
}

static void vga_render_terminal_to(volatile unsigned char *target)
{
    unsigned int position;

    vga_render_shell_chrome_to(target);
    vga_fill_rectangle_to(
        target,
        SHELL_WINDOW_X + 2,
        SHELL_CONTENT_Y - 1,
        SHELL_CLIENT_WIDTH,
        SHELL_CLIENT_HEIGHT + 2,
        SHELL_CONTENT_COLOR);

    for (position = 0; position < VGA_WIDTH * VGA_HEIGHT; ++position)
    {
        unsigned short cell = terminal_cells[position];
        unsigned char character = (unsigned char)cell;

        if (character != ' ')
        {
            vga_render_glyph_to(
                target,
                SHELL_CONTENT_X + (position % VGA_WIDTH) * 8,
                SHELL_CONTENT_Y +
                    (position / VGA_WIDTH) * VGA_TEXT_FONT_HEIGHT,
                character,
                (unsigned char)(cell >> 8) & 0x0F,
                (unsigned char)(cell >> 12));
        }
    }
}

static void vga_render_desktop_to(volatile unsigned char *target)
{
    vga_render_terminal_to(target);
    vga_render_windows_to(target);
    vga_render_mouse_pointer_to(target);
}

static void vga_restore_mouse_pointer(unsigned int page)
{
    unsigned int row;

    if (!mouse_pointer_drawn[page])
    {
        return;
    }

    for (row = 0; row < MOUSE_POINTER_HEIGHT; ++row)
    {
        unsigned int column;
        unsigned int y = mouse_pointer_y + row;

        if (y >= VGA_GRAPHICS_HEIGHT)
        {
            break;
        }
        for (column = 0; column < MOUSE_POINTER_WIDTH; ++column)
        {
            unsigned int x = mouse_pointer_x + column;

            if (x < VGA_GRAPHICS_WIDTH)
            {
                vga_framebuffer[
                    page * VGA_FRAMEBUFFER_SIZE +
                    y * VGA_GRAPHICS_WIDTH + x] =
                    mouse_pointer_background[page][
                        row * MOUSE_POINTER_WIDTH + column];
            }
        }
    }
    mouse_pointer_drawn[page] = 0;
}

static void vga_update_mouse_pointer_background(
    unsigned int page,
    unsigned int x,
    unsigned int y,
    unsigned char color)
{
    unsigned int relative_x;
    unsigned int relative_y;

    if (page > 1 || !mouse_pointer_drawn[page] ||
        x < mouse_pointer_x || y < mouse_pointer_y)
    {
        return;
    }

    relative_x = x - mouse_pointer_x;
    relative_y = y - mouse_pointer_y;
    if (relative_x < MOUSE_POINTER_WIDTH &&
        relative_y < MOUSE_POINTER_HEIGHT)
    {
        mouse_pointer_background[page][
            relative_y * MOUSE_POINTER_WIDTH + relative_x] = color;
    }
}

static void vga_render_mouse_pointer_to(volatile unsigned char *target)
{
    static const char *shape[MOUSE_POINTER_HEIGHT] =
        {
            "#...........",
            "##..........",
            "#W#.........",
            "#WW#........",
            "#WWW#.......",
            "#WWWW#......",
            "#WWWWW#.....",
            "#WWWWWW#....",
            "#WWWWWWW#...",
            "#WWWWWWWW#..",
            "#WWWW#......",
            "#WW#WW#.....",
            "#W#..WW#....",
            "##...WW#....",
            "......WW#...",
            "......WW#...",
            ".......##...",
            "............"
        };
    unsigned int page;
    unsigned int row;

    if (target == vga_framebuffer)
    {
        page = 0;
    }
    else if (target == vga_framebuffer + VGA_FRAMEBUFFER_SIZE)
    {
        page = 1;
    }
    else
    {
        return;
    }

    mouse_pointer_drawn[page] = 0;
    for (row = 0; row < MOUSE_POINTER_HEIGHT; ++row)
    {
        unsigned int column;
        unsigned int y = mouse_pointer_y + row;

        if (y >= VGA_GRAPHICS_HEIGHT)
        {
            break;
        }
        for (column = 0; column < MOUSE_POINTER_WIDTH; ++column)
        {
            unsigned int x = mouse_pointer_x + column;
            unsigned int index = row * MOUSE_POINTER_WIDTH + column;
            unsigned char pixel;

            if (x >= VGA_GRAPHICS_WIDTH)
            {
                break;
            }
            mouse_pointer_background[page][index] =
                target[y * VGA_GRAPHICS_WIDTH + x];
            if (shape[row][column] == '#')
            {
                pixel = 0;
            }
            else if (shape[row][column] == 'W')
            {
                pixel = 15;
            }
            else
            {
                continue;
            }
            target[y * VGA_GRAPHICS_WIDTH + x] = pixel;
            mouse_pointer_drawn[page] = 1;
        }
    }
}

static void vga_render_windows_to(volatile unsigned char *target)
{
    unsigned int slot;

    for (slot = 0; slot < GRAPHICS_WINDOW_LIMIT; ++slot)
    {
        GraphicsWindow *window = &graphics_windows[slot];
        unsigned int row;

        if (!window->visible)
        {
            continue;
        }

        vga_fill_rectangle_to(
            target,
            window->x,
            window->y,
            window->width + 4,
            window->height + SHELL_TITLE_HEIGHT + 4,
            SHELL_WINDOW_BORDER_COLOR);
        vga_fill_rectangle_to(
            target,
            window->x + 1,
            window->y + 1,
            window->width + 2,
            SHELL_TITLE_HEIGHT,
            SHELL_TITLE_COLOR);
        {
            unsigned int title_x = window->x + 8;
            unsigned int title_y = window->y + 8;
            const char *title = window->title;

            while (*title != '\0' &&
                title_x + 8 < window->x + window->width)
            {
                vga_render_glyph_to(
                    target,
                    title_x,
                    title_y,
                    (unsigned char)*title,
                    15,
                    SHELL_TITLE_COLOR);
                title_x += 8;
                ++title;
            }
        }
        for (row = 0; row < window->height; ++row)
        {
            unsigned int column;
            volatile unsigned char *destination = target +
                (window->y + SHELL_TITLE_HEIGHT + 3 + row) *
                    VGA_GRAPHICS_WIDTH + window->x + 2;
            const volatile unsigned char *source =
                window->buffers[window->back_buffer_index ^ 1] +
                row * window->width;

            for (column = 0; column < window->width; ++column)
            {
                destination[column] = source[column];
            }
        }
    }
}

static void vga_render_terminal(void)
{
    vga_render_desktop_to(vga_framebuffer);
}

unsigned int video_create_window(
    unsigned int owner_process_id,
    const char *title,
    unsigned int width,
    unsigned int height)
{
    unsigned int slot;

    if (title == 0 || width == 0 || height == 0 ||
        width > GRAPHICS_WINDOW_MAX_WIDTH ||
        height > GRAPHICS_WINDOW_MAX_HEIGHT)
    {
        return 0;
    }

    for (slot = 0; slot < GRAPHICS_WINDOW_LIMIT; ++slot)
    {
        GraphicsWindow *window = &graphics_windows[slot];
        unsigned int index;

        if (window->owner_process_id != 0 || window->page_count != 0)
        {
            continue;
        }

        for (index = 0;
            index < GRAPHICS_WINDOW_TITLE_LENGTH - 1 &&
                title[index] != '\0';
            ++index)
        {
            window->title[index] = title[index];
        }
        if (index == 0 || title[index] != '\0')
        {
            window->title[0] = '\0';
            return 0;
        }
        window->title[index] = '\0';
        window->owner_process_id = owner_process_id;
        window->width = width;
        window->height = height;
        window->x = (VGA_GRAPHICS_WIDTH - width) / 2;
        window->y =
            (VGA_GRAPHICS_HEIGHT - height - SHELL_TITLE_HEIGHT - 4) / 2;
        window->back_buffer_index = 0;
        window->visible = 1;
        window->page_count = GRAPHICS_WINDOW_PAGE_COUNT;

        for (index = 0; index < GRAPHICS_WINDOW_PAGE_COUNT; ++index)
        {
            unsigned int physical_address = memory_allocate_page();
            unsigned int virtual_address =
                GRAPHICS_WINDOW_VIRTUAL_BASE +
                slot * GRAPHICS_WINDOW_VIRTUAL_STRIDE +
                index * 4096;

            if (physical_address == 0)
            {
                video_destroy_window(owner_process_id, slot + 1);
                return 0;
            }
            window->physical_pages[index] = physical_address;
            if (!memory_map_page(virtual_address, physical_address, 0x02))
            {
                video_destroy_window(owner_process_id, slot + 1);
                return 0;
            }
            window->page_mapped[index] = 1;
        }

        window->buffers[0] = (volatile unsigned char *)(
            GRAPHICS_WINDOW_VIRTUAL_BASE +
            slot * GRAPHICS_WINDOW_VIRTUAL_STRIDE);
        window->buffers[1] =
            window->buffers[0] + GRAPHICS_WINDOW_BUFFER_SIZE;
        graphics_fill_buffer(
            window->buffers[0], width, height, 15);
        graphics_fill_buffer(
            window->buffers[1], width, height, 15);

        vga_render_desktop_to(vga_framebuffer);
        vga_render_desktop_to(
            vga_framebuffer + VGA_FRAMEBUFFER_SIZE);
        vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
        vga_back_buffer_index = 1;
        vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
        return slot + 1;
    }

    return 0;
}

int video_destroy_window(
    unsigned int owner_process_id,
    unsigned int window_handle)
{
    GraphicsWindow *window;
    unsigned int slot;
    unsigned int index;
    int success = 1;

    if (!graphics_window_lookup(owner_process_id, window_handle, &window))
    {
        return 0;
    }
    slot = window_handle - 1;
    window->visible = 0;

    for (index = 0; index < window->page_count; ++index)
    {
        unsigned int virtual_address =
            GRAPHICS_WINDOW_VIRTUAL_BASE +
            slot * GRAPHICS_WINDOW_VIRTUAL_STRIDE +
            index * 4096;
        unsigned int physical_address = window->physical_pages[index];
        unsigned int unmapped_address;

        if (physical_address == 0)
        {
            continue;
        }
        if (window->page_mapped[index])
        {
            if (!memory_unmap_page(virtual_address, &unmapped_address))
            {
                success = 0;
                continue;
            }
            window->page_mapped[index] = 0;
            if (unmapped_address != physical_address)
            {
                kernel_log_value(
                    KERNEL_LOG_ERROR,
                    "video",
                    "unexpected graphics window mapping",
                    window_handle);
                success = 0;
                continue;
            }
        }
        if (!memory_free_page(physical_address))
        {
            success = 0;
            continue;
        }
        else
        {
            window->physical_pages[index] = 0;
        }
        window->page_mapped[index] = 0;
    }

    if (!success)
    {
        kernel_log_value(
            KERNEL_LOG_ERROR,
            "video",
            "failed to release graphics window",
            window_handle);
        return 0;
    }

    window->owner_process_id = 0;
    window->width = 0;
    window->height = 0;
    window->x = 0;
    window->y = 0;
    window->back_buffer_index = 0;
    window->page_count = 0;
    window->buffers[0] = 0;
    window->buffers[1] = 0;
    window->title[0] = '\0';

    vga_render_desktop_to(vga_framebuffer);
    vga_render_desktop_to(vga_framebuffer + VGA_FRAMEBUFFER_SIZE);
    vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
    vga_back_buffer_index = 1;
    vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
    return 1;
}

void video_start_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle)
{
    GraphicsWindow *window;
    unsigned int page;

    if (graphics_window_lookup(owner_process_id, window_handle, &window))
    {
        window->visible = 1;
        graphics_fill_buffer(
            window->buffers[0], window->width, window->height, 15);
        graphics_fill_buffer(
            window->buffers[1], window->width, window->height, 15);
        window->back_buffer_index = 0;
        vga_render_desktop_to(vga_framebuffer);
        vga_render_desktop_to(
            vga_framebuffer + VGA_FRAMEBUFFER_SIZE);
    }
    else if (window_handle == 0)
    {
        shell_graphics_active = 1;
        for (page = 0; page < 2; ++page)
        {
            volatile unsigned char *target =
                vga_framebuffer + page * VGA_FRAMEBUFFER_SIZE;
            vga_render_desktop_to(target);
            vga_fill_rectangle_to(
                target,
                SHELL_CONTENT_X,
                SHELL_CONTENT_Y,
                SHELL_CLIENT_WIDTH,
                SHELL_CLIENT_HEIGHT,
                15);
            vga_render_windows_to(target);
            vga_render_mouse_pointer_to(target);
        }
    }
    else
    {
        return;
    }

    vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
    vga_back_buffer_index = 1;
    vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
}

void video_clear_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle,
    unsigned char color)
{
    GraphicsWindow *window;

    if (graphics_window_lookup(owner_process_id, window_handle, &window))
    {
        graphics_fill_buffer(
            window->buffers[window->back_buffer_index],
            window->width,
            window->height,
            color);
    }
    else if (window_handle == 0 && shell_graphics_active)
    {
        vga_fill_rectangle_to(
            vga_back_buffer,
            SHELL_CONTENT_X,
            SHELL_CONTENT_Y,
            SHELL_CLIENT_WIDTH,
            SHELL_CLIENT_HEIGHT,
            color);
        vga_render_windows_to(vga_back_buffer);
        vga_render_mouse_pointer_to(vga_back_buffer);
    }
}

void video_draw_pixel(
    unsigned int owner_process_id,
    unsigned int window_handle,
    unsigned int x,
    unsigned int y,
    unsigned char color)
{
    GraphicsWindow *window;

    if (graphics_window_lookup(owner_process_id, window_handle, &window))
    {
        if (x < window->width && y < window->height)
        {
            window->buffers[window->back_buffer_index][
                y * window->width + x] = color;
        }
    }
    else if (window_handle == 0 && shell_graphics_active &&
        x < SHELL_CLIENT_WIDTH && y < SHELL_CLIENT_HEIGHT)
    {
        unsigned int screen_x = SHELL_CONTENT_X + x;
        unsigned int screen_y = SHELL_CONTENT_Y + y;

        vga_update_mouse_pointer_background(
            vga_back_buffer_index,
            screen_x,
            screen_y,
            color);
        vga_back_buffer[
            screen_y * VGA_GRAPHICS_WIDTH + screen_x] = color;
    }
}

void video_present_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle)
{
    unsigned int flags;
    GraphicsWindow *window;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }

    if (graphics_window_lookup(owner_process_id, window_handle, &window))
    {
        window->back_buffer_index ^= 1;
        vga_render_desktop_to(vga_back_buffer);
    }
    else if (window_handle == 0 && shell_graphics_active)
    {
        vga_render_shell_graphics_chrome_to(vga_back_buffer);
        vga_render_windows_to(vga_back_buffer);
        vga_render_mouse_pointer_to(vga_back_buffer);
    }
    else
    {
        __asm
        {
            push flags
            popfd
        }
        return;
    }

    vbe_write_register(
        VGA_VBE_INDEX_Y_OFFSET,
        (unsigned short)(vga_back_buffer_index * VGA_GRAPHICS_HEIGHT));
    vga_back_buffer_index ^= 1;
    vga_back_buffer = vga_framebuffer +
        vga_back_buffer_index * VGA_FRAMEBUFFER_SIZE;

    __asm
    {
        push flags
        popfd
    }
}

void video_end_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle)
{
    if (window_handle == 0)
    {
        shell_graphics_active = 0;
    }
    else
    {
        GraphicsWindow *window;

        if (graphics_window_lookup(owner_process_id, window_handle, &window))
        {
            window->visible = 0;
        }
        else
        {
            return;
        }
    }

    vga_render_desktop_to(vga_framebuffer);
    vga_render_desktop_to(vga_framebuffer + VGA_FRAMEBUFFER_SIZE);
    vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
    vga_back_buffer_index = 1;
    vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
}

void video_terminal_restore(void)
{
    shell_graphics_active = 0;
    vga_render_desktop_to(vga_framebuffer);
    vga_render_desktop_to(vga_framebuffer + VGA_FRAMEBUFFER_SIZE);
    vbe_write_register(VGA_VBE_INDEX_Y_OFFSET, 0);
    vga_back_buffer_index = 1;
    vga_back_buffer = vga_framebuffer + VGA_FRAMEBUFFER_SIZE;
}

void video_mouse_move(unsigned int x, unsigned int y)
{
    unsigned int page;

    vga_restore_mouse_pointer(0);
    vga_restore_mouse_pointer(1);
    mouse_pointer_x = x;
    mouse_pointer_y = y;
    for (page = 0; page < 2; ++page)
    {
        vga_render_mouse_pointer_to(
            vga_framebuffer + page * VGA_FRAMEBUFFER_SIZE);
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
    if (!vga_set_graphics_mode())
    {
        kernel_log(
            KERNEL_LOG_ERROR,
            "video",
            "VBE 1280x720 framebuffer setup failed");
        for (;;)
        {
            __asm hlt
        }
    }
    vga_initialize_palette();
    terminal_clear();
    kernel_log(KERNEL_LOG_INFO, "video", "VBE graphics initialized");
}
