# Process loading and exit

MinOS runs each executable as one schedulable thread. The kernel starts
`SHELL.EXE` (built from `src/programs/shell.c`) at boot as an ordinary
foreground process, and restarts it if it ever exits. The kernel's own
scheduler task (task 0, named `kernel`) only idles and supervises the shell.
Every process runs in ring 3 in its own address space. The kernel runs each
process's system calls and interrupts on a per-process kernel stack (selected
through the TSS), and a ring 3 fault (for example touching kernel memory)
terminates only the faulting process.

## Starting a process

The shell program reads a command line, parses it itself (`echo`, trailing `&`,
one `|` pipeline, arguments), and starts each program with system call `33`,
which loads the named ext2 root-directory file via `process_start`. For
example, entering `COUNTDOWN.EXE` asks the filesystem to read that file. For a
foreground command the shell blocks in `wait_process` until the child exits.
The loader
disables interrupts while it prepares the process so that the timer cannot
schedule a partially initialized task.

The loader then:

1. Finds an unused process slot, or appends a new one. The process table lives
   in a shared kernel region and grows on demand, so there is no fixed process
   limit; starting fails with "no memory" or "process limit" only when physical
   memory or that region is exhausted. Each slot also holds the process's
   kernel stack.
2. Creates a new page directory that shares the kernel mappings, then maps
   user-accessible pages starting at `0x40000000` for the executable (up to
   4 MiB) and a 1 MiB stack ending at `0x40800000`. Every process uses
   the same virtual addresses in its own address space.
3. Switches to the new address space and reads the executable file. Both the
   file and loaded image are limited to 4 MiB. MEXE files have a 12-byte
   header containing the signature, image size, and entry offset. For static
   little-endian i386 ELF32 executables, the loader validates and copies
   non-overlapping `PT_LOAD` segments into the range starting at `0x40000000`,
   zeroes BSS, and requires the entry point to be in file-backed executable
   bytes. It rejects dynamic linking, interpreters, and TLS. ELF segment permissions
   are not enforced yet; all loaded image pages are writable and executable.
   In either format, pages beyond the loaded image are freed.
4. Builds an initial saved CPU context on the process's kernel stack. Its
   instruction pointer is the executable's load address plus its entry-point
   offset; its stack pointer is near the top of the user stack. The context
   has ring 3 code/data selectors and interrupts enabled.
5. Assigns a process ID and marks the task active.

The per-process heap begins at `0x41000000` and grows in zeroed 4 KiB
pages up to 16 MiB. ELF staging starts at `0x43000000`, away from the heap,
image and stack. Stack and image pages are zeroed on allocation. Startup exposes
the path passed to the loader as `argv[0]`, space-separated arguments in `argv[1..]`, and a
null-terminated inherited `envp`. The shell's `set NAME=value` changes its
own environment; child processes inherit a copy. Entries are limited to 16
strings of at most 127 characters. ELF `getenv` and `environ` refer to the
startup snapshot. Argument quoting and environment mutation through the ELF
library are not implemented. The runtime constructs these vectors; the raw
entry stack is not a Linux process startup ABI.

When a process ends, the kernel frees all of its pages and its page directory.
The shell prints the process name and ID and continues running. The PIT
generates timer interrupts at 1000 Hz. On each tick, the interrupt stub saves
the current CPU registers and segments on that task's stack. The round-robin
scheduler records the saved stack pointer, selects the next active task, and
returns its saved stack pointer to the interrupt stub. The stub restores that
task's context and returns from the interrupt. This is how the process gets
CPU time and how the shell resumes between process time slices.
The PS/2 mouse is initialized in streaming mode and delivers three-byte motion
packets on IRQ12; the kernel clamps pointer movement to the screen and draws
the pointer above the desktop and process windows. The bottom taskbar lists
the shell and every created graphics window in fixed-width, left-aligned items,
truncating titles longer than 15 characters to a 12-character prefix plus
`...`. Left-clicking a taskbar item or visible window brings it to the
foreground; dragging a graphics window's title bar moves it within the desktop.

Commands normally run in the foreground, and the shell waits for them to exit.
Appending `&` marks a process as a background task; the shell immediately
returns to its prompt while that task remains scheduled. Background tasks do
not receive keyboard input, which keeps shell typing and foreground input
separate. `kill <process-name>` terminates the first active process whose name
matches, case-insensitively; an optional `.exe` suffix or drive/path prefix is
ignored. Process cleanup releases its open streams and memory on a later
timer tick.

## System calls

Programs request kernel services with `int 0x80`. The system-call stub saves
the current context, and the handler dispatches on the value in `EAX`:

| `EAX` | Operation | Arguments / result |
|---|---|---|
| `0` | Exit the process | `EBX` contains the status; its low 8 bits are returned to a waiting parent |
| `1` | Print a string | `EBX` points to a null-terminated string inside the executable image |
| `2` | Read a keyboard event | Returns a make or break scancode in `EAX`, or `0` if no event is queued |
| `3` | Reserved | — |
| `4` | Write the current UTC date and time | No arguments |
| `5` | Report free memory and test a page mapping | No arguments |
| `6` | List active processes and threads | No arguments |
| `7` | Create a directory | Uses the command-line path saved when the program started |
| `8` | Clear the terminal | No arguments |
| `9` | Start graphics output in the process's window | No arguments; terminates the process with a shell error if it has no window |
| `10` | Restore the terminal display | No arguments |
| `11` | Write one character | Character in `EBX` |
| `12` | Open a directory stream | `EBX` points to a string in the executable image or a 128-element integer path array on the process stack; returns a handle or `-1` |
| `13` | Close a directory stream | Stream handle in `EBX` |
| `14` | Read a directory entry | Handle in `EBX`, writable integer array in `ECX`, capacity in `EDX`; returns `1` for an entry, `0` at end, or `-1` on error |
| `15` | Read command-line arguments | Writable integer array in `ECX`, capacity in `EDX`; returns the character count or `-1` |
| `16` | Get current directory | Writable integer array in `ECX`, capacity in `EDX`; returns the path length or `-1` |
| `17` | Change current directory | Uses the command-line arguments saved when the process started; returns `1` on success or `0` on failure |
| `27` | Clear the graphics framebuffer | Palette color in `EBX` |
| `28` | Draw one framebuffer pixel | `EBX` = x, `EDX` = y, `ECX` = palette color |
| `29` | Present the back buffer | No arguments |
| `30` | Create a child process | `EBX` points to a null-terminated executable name in the caller's image; returns the child PID or `-1` |
| `31` | Wait for a child process | Child PID in `EBX`; blocks until it exits and returns its status (0–255), or `-1` on failure. Abnormal termination returns 255 |
| `32` | Create a graphics window | `EBX` points to a non-empty null-terminated title in the caller's image, `ECX` is width (1-640), `EDX` is height (1-480); returns a handle or `0` |
| `33` | Start a program | `EBX` points to a local array with the null-terminated program name, `ECX` to an array with its arguments (each at least 128 elements, one character per element), `EDX` is flags (`1` background, `2` stdout feeds a pipe, `4` stdin reads the pipe started by the preceding `2` call); returns the child PID (a child of the caller unless background) or the negated start error (`-1` invalid name, `-2` arguments too long, `-3` not found, `-4` too large, `-5` disk error, `-6` invalid executable, `-7` no memory, `-8` process limit, `-9` cleanup error) |
| `35` | Sleep | `EBX` is the number of milliseconds (capped at 60000); the process is blocked until the 1000 Hz timer reaches the wake-up time and returns `0` |
| `36` | Grow heap | Maps one zeroed 4 KiB user page, up to 16 MiB per process; returns its address or `0` on failure |
| `37` | Check pipe EOF | Returns `1` when the caller's input pipe has been drained and its producer has closed, or `0` otherwise |
| `38` | Seek file | `EBX` is a file handle, `ECX` a signed offset, and `EDX` is `0` (start), `1` (current), or `2` (end); returns the new position or `-1`. Seeking past EOF is allowed |
| `39` | Read program name | Writable integer array in `ECX`, capacity in `EDX`; returns the path length or `-1` |
| `40` | Read environment entry | `EBX` is an index from 0 to 15, `ECX` is a writable integer array and `EDX` its capacity (at most 128); returns the entry length, zero for an empty slot, or `-1` |
| `41` | Set environment entry | `EBX` points to a `NAME=value` string in the image or a 128-element integer array on the stack; returns `0` or `-1`. Names start with a letter or underscore and contain only letters, digits and underscores |
| `42` | Open or create regular file | `EBX` points to a path; opens an existing file or creates it on `B:` and returns a handle, or `-1` |
| `43` | Create directory by path | `EBX` points to a path; returns a `CreateDirectoryResult` (`0` on success), or `-1` for an invalid buffer. Creation is restricted to `B:` |
| `44` | Change directory by path | `EBX` points to a path; returns `0` on success or `-1` |
| `45` | Create local socket | Returns a new process-owned endpoint handle or `-1` |
| `46` | Listen on local port | `EBX` is a new endpoint, `ECX` is a port from 1 to 65535; returns `0` or `-1` (including an already-bound port) |
| `47` | Connect to local port | `EBX` is a new endpoint, `ECX` is the port; returns `0`, `-1` for invalid input or no listener, or `-2` if endpoint capacity is exhausted |
| `48` | Accept local connection | Listener in `EBX`; returns a connected endpoint, `-2` when none are pending, or `-1` on error |
| `49` | Send local bytes | Endpoint in `EBX`, stack integer array in `ECX`, length in `EDX` (0–256); returns bytes sent, `-2` if the peer buffer is full, or `-1` on error or peer closure |
| `50` | Receive local bytes | Endpoint in `EBX`, writable stack integer array in `ECX`, length in `EDX` (0–256); returns bytes received, `-2` when empty, `0` after peer closure and draining, or `-1` on error |
| `51` | Close local socket | Endpoint in `EBX`; returns `0` or `-1` |

These local streams do not implement TCP/IP or POSIX sockets. There are 32
endpoints system-wide, including listeners and pending connections, and each
endpoint has a 1024-byte receive buffer. Send can write fewer bytes than
requested; applications must retry the remainder. For nonzero receive
lengths, zero means EOF; zero-length send and receive return zero. Closing a
listener closes its unaccepted connections, and process termination closes
all of its endpoints. Buffered data remains readable after the peer closes.
The kernel runs and supervises `HTTPSRV.EXE` on local port 8000; `httpget`
is a sample client.

The kernel checks that a print pointer and its terminating null byte are
within the process image. Keyboard events are queued for active processes;
break scancodes have bit 7 set, and the Escape make scancode is `1`.
Unsupported calls or invalid print requests terminate the calling process.
Directory paths support absolute and current-directory-relative paths,
including `.` and `..`; symbolic links are not supported. The current directory
is shared by shell-launched programs. Drive `A:` is the system disk and drive
`B:` is the secondary ext2 data disk; absolute paths such as `A:/ball.exe` and
`B:/SAMPLE.TXT` select the corresponding disk. The kernel writes each entry name as
integer character values into the caller's validated stack buffer. Open streams
are released automatically when their owning process exits. Directory
traversal, hidden-name filtering, and listing output are implemented in C;
the services only open, read, and close streams.
Drawing, terminal input, and BASIC parsing and interpretation are also
implemented in C; kernel services expose graphics setup/restore, per-process
window creation and double-buffered drawing, and character output. A process
must create a window before starting graphics mode; attempting to start
graphics without one reports an error to the shell and terminates the process.
A process-created window is isolated from other processes' drawing.

Processes started with a trailing `&` run in the background without blocking
the shell. Background processes cannot change the terminal display or draw to
the shared graphics framebuffer, so a graphical program can run without hiding
the shell; terminate it with `kill <name>`.

## Exiting a process

When a program invokes exit (`EAX=0`), the system-call handler marks the
current process as dying and selects another active task. The interrupt stub
switches to that task's saved stack and restores its context. Usually this is
the shell, which resumes where it was interrupted and can accept another
command. Exiting a process does not exit MinOS.

The dying process's memory is not freed while its stack is still in use.
During a later timer tick, the scheduler releases its executable and stack
page mappings and returns their physical pages to the allocator. Once cleanup
succeeds, the slot becomes available for another program and the shell's
saved terminal contents are redrawn. Any graphics window owned by the process
is also hidden and its framebuffer pages are released. If cleanup fails, the task is marked
with an error state and its mappings are retained rather than treated as
successfully freed.

The loader uses the same cleanup path if loading or executable validation
fails partway through. In that case the shell reports the specific start
failure and remains available.

## File services

| `EAX` | Operation | Arguments / result |
|---|---|---|
| `18` | Open file | Path in `EBX`; returns a handle or `-1` |
| `19` | Read file | Handle in `EBX`, integer buffer in `ECX`, capacity up to 256 in `EDX`; returns bytes, `0` at end, or `-1` |
| `20` | Close file | Handle in `EBX` |
| `21` | Read pipeline input (legacy) | Writable integer buffer in `ECX`, capacity up to 256 in `EDX`; returns bytes, `0` while empty, or `-1` at end of input |
| `22` | Write file | Handle in `EBX`, integer buffer in `ECX`, byte count up to 256 in `EDX`; returns bytes written or `-1` |
| `23` | Truncate file | Handle in `EBX`; resets size and stream position to zero or returns `-1` |
| `24` | Read standard stream | Handle in `EBX`, writable integer buffer in `ECX`, capacity up to 256 in `EDX`; returns bytes read, `0` if input is not ready, or `-1` |
| `25` | Write standard stream | Handle in `EBX`, integer buffer in `ECX`, byte count up to 256 in `EDX`; returns bytes written or `-1` |

File paths may be absolute or relative to the shared current directory. Reads
return byte values in a validated integer buffer. Open files are closed
automatically when the owning process exits.

Each process starts with `STD_INPUT_HANDLE` (`0`), `STD_OUTPUT_HANDLE` (`1`),
and `STD_ERROR_HANDLE` (`2`) bound to the shell's keyboard and terminal.
Standard input returns available keyboard characters without blocking; when
used as a pipeline consumer it reads from the pipe instead. Standard output
and error both write to the shell terminal, but only standard output is
redirected into a pipeline.

An ext2 data disk is attached as the secondary IDE drive. `A:/` resolves to
the system disk and `B:/` resolves to the secondary data disk.
File and directory creation, writes, and truncation are restricted to this
secondary disk. File writes can allocate up to 12 direct 1 KiB blocks. A write
after a seek past EOF fills the gap with zeroes, up to that limit; a failed
write may leave a partially zero-filled extension. Directory creation accepts names of one to eight
letters, digits, underscores or hyphens. The ext2 metadata updates are not
journaled or transactional, so an I/O failure or interruption can leave an
inconsistent volume. Simultaneous streams of one file are not coherent.

The shell accepts a single pipeline (`program [args] | program [args]`). The
producer's standard output becomes the consumer's standard input;
`read_stream(STD_INPUT_HANDLE, buffer)` in C reads up to 256 bytes per call.
The kernel uses one 16 KiB in-memory pipe buffer, so output exceeding its
capacity before the consumer reads it terminates the producer with an error.
