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

enum FileReadResult
{
    FILE_READ_OK,
    FILE_READ_INVALID_NAME,
    FILE_READ_NOT_FOUND,
    FILE_READ_TOO_LARGE,
    FILE_READ_DISK_ERROR,
    FILE_READ_CORRUPT
};

enum ProcessStartResult
{
    PROCESS_START_OK,
    PROCESS_START_INVALID_NAME,
    PROCESS_START_ARGUMENT_TOO_LONG,
    PROCESS_START_NOT_FOUND,
    PROCESS_START_TOO_LARGE,
    PROCESS_START_DISK_ERROR,
    PROCESS_START_INVALID_EXECUTABLE,
    PROCESS_START_NO_MEMORY,
    PROCESS_START_LIMIT_REACHED,
    PROCESS_START_CLEANUP_ERROR
};

enum ProcessService
{
    PROCESS_SERVICE_LIST_ROOT = 3,
    PROCESS_SERVICE_CLOCK,
    PROCESS_SERVICE_MEMORY_REPORT,
    PROCESS_SERVICE_LIST_PROCESSES,
    PROCESS_SERVICE_MAKE_DIRECTORY,
    PROCESS_SERVICE_TERMINAL_CLEAR,
    PROCESS_SERVICE_DRAW_CIRCLE,
    PROCESS_SERVICE_BASIC_REPL
};

void terminal_write_character(char character);
void terminal_write(const char *text);
void terminal_write_prompt(const char *text);
void terminal_write_unsigned(unsigned int value);
void terminal_clear(void);
void video_terminal_restore(void);
void video_initialize(void);
void video_draw_circle(void);
void interrupts_initialize(void);
void processes_initialize(void);
enum ProcessStartResult process_start(
    const char *filename,
    const char *arguments,
    unsigned int *process_id);
void process_list(void);
int process_has_active_process(void);
unsigned char process_keyboard_read_scancode(void);
void shell_run_service(unsigned int service, const char *arguments);
unsigned int __cdecl process_timer_tick(unsigned int *saved_stack);
unsigned int __cdecl process_system_call(unsigned int *saved_stack);
void process_keyboard_scancode(unsigned char scancode);
void clock_write_time(void);
void keyboard_handle_irq(void);
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

void filesystem_list_root(void);
enum FileReadResult filesystem_read_root_file(
    const char *name,
    unsigned char *buffer,
    unsigned int buffer_size,
    unsigned int *file_size);
enum CreateDirectoryResult filesystem_create_root_directory(
    const char *name);

void basic_repl(void);

#endif
