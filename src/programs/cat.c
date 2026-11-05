#include <stdio.h>
#include <minos.h>

int main() {
    int path[128];
    int contents[256];

    if (read_arguments(path) <= 0) {
        printf("Usage: cat <file>\n");
        return 0;
    }

    int file = open_file(path);
    if (file < 0) {
        printf("cat: could not open file.\n");
        return 0;
    }

    int count = read_file(file, contents);
    while (count > 0) {
        for (int index = 0; index < count; index++) {
            putchar(contents[index]);
        }
        count = read_file(file, contents);
    }

    close_file(file);
    if (count < 0) {
        printf("\ncat: could not read file.\n");
    }
    return 0;
}
