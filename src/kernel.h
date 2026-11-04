#ifndef MINOS_KERNEL_H
#define MINOS_KERNEL_H

#define COMMAND_BUFFER_SIZE 128

void terminal_write_character(char character);
void terminal_write(const char *text);
void terminal_write_prompt(const char *text);
void terminal_write_unsigned(unsigned int value);
void terminal_clear(void);
void video_terminal_restore(void);
void video_initialize(void);
void video_start_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle);
void video_end_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle);
void video_clear_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle,
    unsigned char color);
void video_draw_pixel(
    unsigned int owner_process_id,
    unsigned int window_handle,
    unsigned int x,
    unsigned int y,
    unsigned char color);
void video_present_graphics(
    unsigned int owner_process_id,
    unsigned int window_handle);
void video_mouse_move(unsigned int x, unsigned int y);
unsigned int video_create_window(
    unsigned int owner_process_id,
    const char *title,
    unsigned int width,
    unsigned int height);
int video_destroy_window(
    unsigned int owner_process_id,
    unsigned int window_handle);
void interrupts_initialize(void);
void clock_write_time(void);
void keyboard_handle_irq(void);
void mouse_handle_irq(void);
int mouse_initialize(void);
void memory_initialize(void);
unsigned int memory_allocate_page(void);
int memory_free_page(unsigned int physical_address);
int memory_map_page(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags);
int memory_unmap_page(
    unsigned int virtual_address,
    unsigned int *physical_address);
int memory_translate(
    unsigned int virtual_address,
    unsigned int *physical_address);
unsigned int memory_available_pages(void);

int is_space(char character);
int command_equals(const char *command, const char *expected);
int command_token_equals(
    const char *command,
    const char *end,
    const char *expected);
char keyboard_read_character(
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended);
char keyboard_decode_scancode(
    unsigned char scancode,
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended);

#endif
