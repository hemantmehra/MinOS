#ifndef MINOS_PROCESS_H
#define MINOS_PROCESS_H

#include "kernel.h"

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
    PROCESS_SERVICE_CLOCK = 4,
    PROCESS_SERVICE_MEMORY_REPORT,
    PROCESS_SERVICE_LIST_PROCESSES,
    PROCESS_SERVICE_MAKE_DIRECTORY,
    PROCESS_SERVICE_TERMINAL_CLEAR,
    PROCESS_SERVICE_START_GRAPHICS,
    PROCESS_SERVICE_END_GRAPHICS,
    PROCESS_SERVICE_WRITE_CHARACTER,
    PROCESS_SERVICE_OPENDIR,
    PROCESS_SERVICE_CLOSEDIR,
    PROCESS_SERVICE_READ_DIR,
    PROCESS_SERVICE_READ_ARGUMENTS,
    PROCESS_SERVICE_GET_CURRENT_DIRECTORY,
    PROCESS_SERVICE_CHANGE_DIRECTORY,
    PROCESS_SERVICE_OPEN_FILE,
    PROCESS_SERVICE_READ_FILE,
    PROCESS_SERVICE_CLOSE_FILE,
    PROCESS_SERVICE_READ_PIPE,
    PROCESS_SERVICE_WRITE_FILE,
    PROCESS_SERVICE_TRUNCATE_FILE,
    PROCESS_SERVICE_READ_STREAM,
    PROCESS_SERVICE_WRITE_STREAM,
    PROCESS_SERVICE_KILL_PROCESS = 26,
    PROCESS_SERVICE_CLEAR_GRAPHICS,
    PROCESS_SERVICE_DRAW_PIXEL,
    PROCESS_SERVICE_PRESENT_GRAPHICS,
    PROCESS_SERVICE_CREATE_PROCESS,
    PROCESS_SERVICE_WAIT_PROCESS,
    PROCESS_SERVICE_CREATE_WINDOW,
    PROCESS_SERVICE_START_PROCESS,
    PROCESS_SERVICE_TERMINAL_HOME
};

enum ProcessStandardHandle
{
    STD_INPUT_HANDLE = 0,
    STD_OUTPUT_HANDLE,
    STD_ERROR_HANDLE
};

void processes_initialize(void);
enum ProcessStartResult process_start(
    const char *filename,
    const char *arguments,
    unsigned int *process_id,
    int background);
void process_list(void);
int process_has_active_process(void);
int process_has_foreground_process(void);
int process_kill_by_name(
    unsigned int caller_process_id, const char *name);
unsigned char process_keyboard_read_scancode(void);
unsigned int __cdecl process_timer_tick(unsigned int *saved_stack);
unsigned int __cdecl process_system_call(unsigned int *saved_stack);
void process_keyboard_scancode(unsigned char scancode);
void shell_run_service(
    unsigned int service,
    const char *arguments,
    unsigned int owner_process_id,
    unsigned int window_handle);
#endif
