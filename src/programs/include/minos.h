#ifndef MINOS_H
#define MINOS_H

/* Programs may declare `int main(int argc, char *argv[])`. The runtime splits
   the command line on spaces: argv[0] is an empty string, argv[1..] are the
   arguments (at most 15). Plain `int main()` is also accepted. */

/* Prints prompt, then reads a line of at most max (<= 127) characters from
   the keyboard. Returns a pointer to a static buffer. */
char *read_line(const char *prompt, int max);

/* Parses decimal digits starting at text + offset. Returns -1 if there are
   no digits or any non-digit character. */
int parse_int(const char *text, int offset);

/* Returns a pending keyboard scancode, or 0 when no key is available. */
int read_key(void);

/* Returns the character for a set 1 key-press scancode (shift selects the
   shifted table), or 0 if the key has no printable character. */
int key_char(int scancode, int shift);

/* Returns a pseudo-random number in [0, limit). */
int random(int limit);

/* Kernel services. Buffers passed to the kernel must be local int arrays
   (one character per element) of at least the documented size. */

void clock(void);
void memory_report(void);
void process_list(void);
void make_directory(void);
void terminal_clear(void);

/* Filesystem. Paths are string literals or local int arrays of 128. Handle
   functions return a negative value on failure. */
int opendir(const void *path);
int closedir(int handle);
int read_dir(int handle, int *name);          /* name: 256 elements */
int open_file(const void *path);
int close_file(int handle);
int read_file(int handle, int *buffer);       /* buffer: 256 elements */
int write_file(int handle, int *buffer, int length);
int truncate_file(int handle);
int get_current_directory(int *path);         /* path: 128 elements */
int change_directory(void);

/* Standard streams: 0 = input, 1 = output. */
int read_stream(int handle, int *buffer);     /* buffer: 256 elements */
int write_stream(int handle, int *buffer, int length);

/* Processes. */
int create_process(const char *name);
int start_process(const void *name, const void *arguments, int flags);
int wait_process(int process_id);
int kill_process(void);

/* Graphics. A window is required before drawing. */
int create_window(const char *title, int width, int height);
void start_graphics_mode(void);
void end_graphics_mode(void);
void clear_graphics(int color);
void draw_pixel(int x, int y, int color);
void present(void);

#endif
