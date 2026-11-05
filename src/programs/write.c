#include <stdio.h>
#include <minos.h>

int main(int argc, char *argv[]) {
    int contents[256];

    if (argc < 3) {
        printf("Usage: write B:/file <text>\n");
        return 0;
    }

    int length = 0;
    for (int arg = 2; arg < argc; arg++) {
        if (arg > 2) {
            if (length >= 256) {
                printf("write: text is limited to 256 bytes.\n");
                return 0;
            }
            contents[length] = ' ';
            length++;
        }
        for (int index = 0; argv[arg][index] != 0; index++) {
            if (length >= 256) {
                printf("write: text is limited to 256 bytes.\n");
                return 0;
            }
            contents[length] = argv[arg][index];
            length++;
        }
    }

    int handle = open_file(argv[1]);
    if (handle < 0) {
        printf("write: could not open file. Use an existing B: file.\n");
        return 0;
    }
    if (truncate_file(handle) < 0) {
        close_file(handle);
        printf("write: writing is supported only on B: files.\n");
        return 0;
    }
    int result = write_file(handle, contents, length);
    close_file(handle);

    if (result < 0) {
        printf("write: disk write failed or file capacity was exceeded.\n");
    } else {
        printf("Wrote %d bytes.\n", result);
    }
    return 0;
}
