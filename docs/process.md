# Process loading and exit

MinOS runs each executable as one schedulable thread. The shell is also a
scheduler task, so starting a program does not replace or shut down the shell.
Processes currently run in ring 0 and share the kernel address space; they are
not isolated from the kernel or from one another.

## Starting a process

When a command is not a built-in shell command, the shell passes it to
`process_start` as an ext2 root-directory filename. For example, entering
`COUNTDOWN.EXE` asks the filesystem to read that file. The loader disables
interrupts while it prepares the process so that the timer cannot schedule a
partially initialized task.

The loader then:

1. Finds an unused process slot. The kernel supports up to eight processes in
   addition to the shell.
2. Reserves a 2 MiB virtual-address slot starting at `0x40000000`. It allocates
   physical pages for the executable and a four-page stack, and maps those
   pages into the process slot.
3. Reads the root-directory file into the slot. Executables are limited to
   64 KiB and use the MinOS `MEXE` format: a 12-byte header containing the
   four-byte `MEXE` signature, the total image size, and the entry-point
   offset, followed by 32-bit x86 code and data. The loader checks the
   signature, file size, and that the entry point is inside the image.
4. Builds an initial saved CPU context. Its instruction pointer is the
   executable's load address plus its entry-point offset; its stack pointer
   is at the top of its stack. The context has kernel code/data selectors and
   interrupts enabled.
5. Assigns a process ID and marks the task active.

The executable pages are mapped writable, and processes execute in ring 0.
The current page mappings do not provide privilege isolation; processes share
the kernel address space and can access kernel memory.

The shell prints the process name and ID and continues running. The PIT
generates timer interrupts at 100 Hz. On each tick, the interrupt stub saves
the current CPU registers and segments on that task's stack. The round-robin
scheduler records the saved stack pointer, selects the next active task, and
returns its saved stack pointer to the interrupt stub. The stub restores that
task's context and returns from the interrupt. This is how the process gets
CPU time and how the shell resumes between process time slices.

## System calls

Programs request kernel services with `int 0x80`. The system-call stub saves
the current context, and the handler dispatches on the value in `EAX`:

| `EAX` | Operation | Arguments / result |
|---|---|---|
| `0` | Exit the process | No arguments |
| `1` | Print a string | `EBX` points to a null-terminated string inside the executable image |
| `2` | Read a keyboard event | Returns a make or break scancode in `EAX`, or `0` if no event is queued |
| `3` | Reserved | — |
| `4` | Write the current UTC date and time | No arguments |
| `5` | Report free memory and test a page mapping | No arguments |
| `6` | List active processes and threads | No arguments |
| `7` | Create a directory | Uses the command-line path saved when the program started |
| `8` | Clear the terminal | No arguments |
| `9` | Initialize graphics output with a white framebuffer | No arguments |
| `10` | Restore the terminal display | No arguments |
| `11` | Write one character | Character in `EBX` |
| `12` | Open a directory stream | `EBX` points to a string in the executable image or a 128-element integer path array on the process stack; returns a handle or `-1` |
| `13` | Close a directory stream | Stream handle in `EBX` |
| `14` | Read a directory entry | Handle in `EBX`, writable integer array in `ECX`, capacity in `EDX`; returns `1` for an entry, `0` at end, or `-1` on error |
| `15` | Read command-line arguments | Writable integer array in `ECX`, capacity in `EDX`; returns the character count or `-1` |
| `16` | Get current directory | Writable integer array in `ECX`, capacity in `EDX`; returns the path length or `-1` |
| `17` | Change current directory | Uses the command-line arguments saved when the process started; returns `1` on success or `0` on failure |

The kernel checks that a print pointer and its terminating null byte are
within the process image. Keyboard events are queued for active processes;
break scancodes have bit 7 set, and the Escape make scancode is `1`.
Unsupported calls or invalid print requests terminate the calling process.
Directory paths support absolute and current-directory-relative paths,
including `.` and `..`; symbolic links are not supported. The current directory
is shared by shell-launched programs. `/data` is a visible mount point for the
secondary ext2 disk; changing directory and creating directories beneath it
operate on that disk. The kernel writes each entry name as
integer character values into the caller's validated stack buffer. Open streams
are released automatically when their owning process exits. Directory
traversal, hidden-name filtering, and listing output are implemented in High;
the services only open, read, and close streams.
Drawing, terminal input, and BASIC parsing and interpretation are also
implemented in High; kernel services expose only graphics setup/restore and
character output for those operations.

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
saved terminal contents are redrawn. If cleanup fails, the task is marked
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
| `21` | Read pipeline input | Writable integer buffer in `ECX`, capacity up to 256 in `EDX`; returns bytes, `0` while empty, or `-1` at end of input |
| `22` | Write file | Handle in `EBX`, integer buffer in `ECX`, byte count up to 256 in `EDX`; returns bytes written or `-1` |
| `23` | Truncate file | Handle in `EBX`; resets size and stream position to zero or returns `-1` |

File paths may be absolute or relative to the shared current directory. Reads
return byte values in a validated integer buffer. Open files are closed
automatically when the owning process exits.

An ext2 data disk is attached as the secondary IDE drive. Absolute paths under
`/data` resolve from its root; the primary system disk remains unchanged.
Writes and truncation are restricted to this secondary disk. Files must already
exist and have allocated data blocks; writes cannot grow beyond allocated
capacity.

The shell accepts a single pipeline (`program [args] | program [args]`). The
producer's printed output becomes the consumer's input; `read_pipe(buffer)` in
High reads up to 256 bytes per call. The kernel uses one 16 KiB in-memory pipe
buffer, so output exceeding its capacity before the consumer reads it terminates
the producer with an error.
