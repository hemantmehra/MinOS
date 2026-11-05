#include <stdio.h>
#include <minos.h>

int main() {
    int path[128];
    int name[256];

    if (read_arguments(path) < 0) {
        printf("ls: path argument is too long.\n");
        return 0;
    }

    int directory = opendir(path);
    if (directory < 0) {
        printf("ls: could not open directory.\n");
        return 0;
    }

    int result = read_dir(directory, name);
    while (result > 0) {
        if (name[0] != '.') {
            for (int index = 0; name[index] != 0; index++) {
                putchar(name[index]);
            }
            printf("  ");
        }
        result = read_dir(directory, name);
    }

    closedir(directory);
    if (result < 0) {
        printf("ls: could not read directory.\n");
    } else {
        printf("\n");
    }
    return 0;
}
