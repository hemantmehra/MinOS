#ifndef PROCESS_INTERNAL_H
#define PROCESS_INTERNAL_H

#include "process.h"

#define TASK_LIMIT process_count
#define PROCESS_TABLE_BASE 0xC0000000
#define PROCESS_TABLE_SIZE 0x01000000
#define PROCESS_KERNEL_STACK_SIZE 14336
#define EXECUTABLE_MAX_SIZE (4 * 1024 * 1024)
#define EXECUTABLE_PAGE_COUNT (EXECUTABLE_MAX_SIZE / 4096)
#define PROCESS_STACK_PAGE_COUNT 256
#define PROCESS_SLOT_SIZE 0x00800000
#define PROCESS_BASE 0x40000000
#define PROCESS_HEAP_BASE 0x41000000
#define PROCESS_HEAP_PAGES 4096
#define PROCESS_ENV_COUNT 16
#define PROCESS_ENV_LENGTH 128
#define USER_CODE_SELECTOR 0x1B
#define USER_DATA_SELECTOR 0x23
#define USER_PAGE_FLAGS 0x06
#define PROCESS_PIPE_CAPACITY 16384
#define PROCESS_SYSCALL_PRINT 1
#define PROCESS_SYSCALL_KEYBOARD_READ 2
#define PROCESS_SYSCALL_EXIT 0
#define PROCESS_KEYBOARD_QUEUE_SIZE 16
#define PROCESS_KEYBOARD_QUEUE_MASK (PROCESS_KEYBOARD_QUEUE_SIZE - 1)
#define PROCESS_STANDARD_INPUT_QUEUE_SIZE 64
#define PROCESS_STANDARD_INPUT_QUEUE_MASK \
    (PROCESS_STANDARD_INPUT_QUEUE_SIZE - 1)

enum TaskState
{
    TASK_UNUSED,
    TASK_LOADING,
    TASK_ACTIVE,
    TASK_WAITING,
    TASK_SLEEPING,
    TASK_DYING,
    TASK_EXITED,
    TASK_CLEANUP_ERROR
};

typedef struct
{
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int original_esp;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
    unsigned int user_esp;
    unsigned int user_ss;
} SavedContext;

typedef struct
{
    enum TaskState state;
    unsigned int process_id;
    unsigned int parent_process_id;
    unsigned int wait_process_id;
    unsigned int exit_status;
    unsigned int wake_tick;
    unsigned int window_handle;
    unsigned int *saved_stack;
    unsigned int page_directory;
    unsigned int code_base;
    unsigned int code_size;
    unsigned int entry_offset;
    unsigned int heap_pages;
    volatile unsigned char keyboard_events[PROCESS_KEYBOARD_QUEUE_SIZE];
    volatile unsigned char keyboard_event_read;
    volatile unsigned char keyboard_event_write;
    volatile unsigned char standard_input[PROCESS_STANDARD_INPUT_QUEUE_SIZE];
    volatile unsigned char standard_input_read;
    volatile unsigned char standard_input_write;
    unsigned char keyboard_shift_pressed;
    unsigned char keyboard_caps_lock;
    unsigned char keyboard_extended;
    char arguments[COMMAND_BUFFER_SIZE];
    char program_path[128];
    char environment[PROCESS_ENV_COUNT][PROCESS_ENV_LENGTH];
    char name[13];
    unsigned int standard_input_handle;
    unsigned int standard_output_handle;
    unsigned int standard_error_handle;
    unsigned char background;
    unsigned int input_pipe;
    unsigned int output_pipe;
    unsigned char kernel_stack[PROCESS_KERNEL_STACK_SIZE];
} Process;

typedef struct
{
    int active;
    unsigned int producer_id;
    unsigned int consumer_id;
    unsigned int read_position;
    unsigned int write_position;
    unsigned int buffered;
    unsigned char producer_closed;
    unsigned char consumer_closed;
    unsigned char overflowed;
    unsigned char data[PROCESS_PIPE_CAPACITY];
} ProcessPipe;

#define SPAWN_BACKGROUND 1
#define SPAWN_PIPE_OUTPUT 2
#define SPAWN_PIPE_INPUT 4

#define processes ((Process *)PROCESS_TABLE_BASE)
extern unsigned int process_count;
extern unsigned int process_table_mapped;
extern ProcessPipe process_pipe;
extern unsigned int current_task;
extern unsigned int next_process_id;
extern volatile unsigned int tick_count;

int process_load_elf(unsigned int file_size, unsigned int *entry_offset,
    unsigned int *image_size);
void process_inherit_environment(Process *child, const Process *parent);

static __inline unsigned int disable_interrupts(void)
{
    unsigned int flags;

    __asm
    {
        pushfd
        pop eax
        mov flags, eax
        cli
    }
    return flags;
}

static __inline void restore_interrupts(unsigned int flags)
{
    __asm
    {
        push flags
        popfd
    }
}

static __inline unsigned int read_little_endian_dword(const unsigned char *data)
{
    return (unsigned int)data[0] |
        ((unsigned int)data[1] << 8) |
        ((unsigned int)data[2] << 16) |
        ((unsigned int)data[3] << 24);
}

int process_release(Process *process, unsigned int slot);

unsigned int process_kernel_stack_top(const Process *process);

void process_activate_current(void);

void process_initialize_slot(Process *process);

int process_table_grow(void);

int process_abort(Process *process, unsigned int slot);

void process_cleanup_terminated(void);

unsigned int find_next_active_task(void);

int process_keyboard_try_read_scancode(
    Process *process,
    unsigned char *scancode);

int process_pipe_write(
    unsigned int pipe_handle,
    const unsigned char *data,
    unsigned int length);

int process_pipe_read(
    unsigned int pipe_handle,
    unsigned int *destination,
    unsigned int capacity);

int process_stream_read(
    Process *process,
    unsigned int handle,
    unsigned int *destination,
    unsigned int capacity);

int process_stream_write(
    Process *process,
    unsigned int handle,
    const unsigned char *data,
    unsigned int length);

void process_pipe_close_endpoint(const Process *process);

void process_terminate(Process *process);

void process_terminate_current(void);

int process_print_string(
    Process *process,
    unsigned int address);

int process_stack_buffer_is_valid(
    const Process *process,
    unsigned int address,
    unsigned int byte_count);

int process_copy_user_path(
    const Process *process,
    unsigned int address,
    char path[128]);

int process_copy_user_title(
    const Process *process,
    unsigned int address,
    char title[32]);

int process_copy_string_to_array(
    const char *source,
    unsigned int *destination,
    unsigned int capacity);

Process *process_find_active(unsigned int process_id);

int process_spawn(
    const Process *parent,
    const char *filename,
    const char *arguments,
    unsigned int spawn_flags);

#endif
