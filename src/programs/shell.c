/* The MinOS shell, started by the kernel at boot. It parses each command line
   (echo, `&` background, one `|` pipeline, arguments), starts programs with
   start_process, and waits for foreground programs with wait_process. */
#include <stdio.h>
#include <minos.h>

void print_start_error(int code) {
    if (code == 1) {
        printf("run: use an 8.3 executable name, such as HELLO.EXE.\n");
    } else if (code == 2) {
        printf("run: command arguments are too long.\n");
    } else if (code == 3) {
        printf("run: executable not found.\n");
    } else if (code == 4) {
        printf("run: executable exceeds the 64 KiB limit.\n");
    } else if (code == 5) {
        printf("run: failed to read executable from disk.\n");
    } else if (code == 6) {
        printf("run: invalid MEXE executable.\n");
    } else if (code == 7) {
        printf("run: could not allocate executable memory.\n");
    } else if (code == 8) {
        printf("run: process limit reached (9 processes).\n");
    } else {
        printf("run: could not start the process.\n");
    }
}

int main() {
    int name[128];
    int args[128];

    while (1) {
        char *line = read_line("minios> ", 127);

        int end = 0;
        while (line[end] != 0) {
            end++;
        }
        while (end > 0 && line[end - 1] == ' ') {
            end--;
        }

        int background = 0;
        if (end > 0 && line[end - 1] == '&') {
            background = 1;
            end--;
            while (end > 0 && line[end - 1] == ' ') {
                end--;
            }
        }

        int pipes = 0;
        int pipe_at = -1;
        for (int pos = 0; pos < end; pos++) {
            if (line[pos] == '|') {
                pipes++;
                pipe_at = pos;
            }
        }

        if (end == 0) {
            continue;
        }
        if (pipes > 1) {
            printf("Usage: <command> | <command>\n");
            continue;
        }
        int stages = 1;
        if (pipes == 1) {
            stages = 2;
        }

        int pid0 = 0;
        int pid1 = 0;
        for (int stage = 0; stage < stages; stage++) {
            int seg_start = 0;
            int seg_end = end;
            if (stages == 2) {
                if (stage == 0) {
                    seg_end = pipe_at;
                } else {
                    seg_start = pipe_at + 1;
                }
            }
            while (seg_start < seg_end && line[seg_start] == ' ') {
                seg_start++;
            }
            while (seg_end > seg_start && line[seg_end - 1] == ' ') {
                seg_end--;
            }
            if (seg_start == seg_end) {
                printf("Usage: <command> | <command>\n");
                break;
            }

            int name_end = seg_start;
            int count = 0;
            while (name_end < seg_end && line[name_end] != ' ') {
                name[count] = line[name_end];
                count++;
                name_end++;
            }
            name[count] = 0;

            int arg_start = name_end;
            while (arg_start < seg_end && line[arg_start] == ' ') {
                arg_start++;
            }
            count = 0;
            for (int pos = arg_start; pos < seg_end; pos++) {
                args[count] = line[pos];
                count++;
            }
            args[count] = 0;

            if (stages == 1 && name_end - seg_start == 4 && name[0] == 'e' &&
                name[1] == 'c' && name[2] == 'h' && name[3] == 'o') {
                for (int pos = arg_start; pos < seg_end; pos++) {
                    putchar(line[pos]);
                }
                printf("\n");
                break;
            }

            int flags = background;
            if (stages == 2) {
                if (stage == 0) {
                    flags += 2;
                } else {
                    flags += 4;
                }
            }
            int pid = start_process(name, args, flags);
            if (pid < 0) {
                print_start_error(-pid);
                break;
            }
            if (stage == 0) {
                pid0 = pid;
            } else {
                pid1 = pid;
            }
        }

        if (!background) {
            if (pid0 > 0) {
                wait_process(pid0);
            }
            if (pid1 > 0) {
                wait_process(pid1);
            }
        }
    }
    return 0;
}
