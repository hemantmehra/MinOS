#ifndef MINOS_KERNEL_H
#define MINOS_KERNEL_H

#define COMMAND_BUFFER_SIZE 128

enum CreateDirectoryResult
{
    CREATE_DIRECTORY_OK,
    CREATE_DIRECTORY_INVALID_NAME,
    CREATE_DIRECTORY_EXISTS,
    CREATE_DIRECTORY_ROOT_FULL,
    CREATE_DIRECTORY_DISK_FULL,
    CREATE_DIRECTORY_IO_ERROR,
    CREATE_DIRECTORY_FILESYSTEM_ERROR
};

void terminal_write_character(char character);
void terminal_write(const char *text);
void terminal_write_unsigned(unsigned int value);
void terminal_clear(void);
void video_initialize(void);
void video_draw_circle(void);

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

void filesystem_list_root(void);
enum CreateDirectoryResult filesystem_create_root_directory(
    const char *name);

void basic_repl(
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended);

#endif
