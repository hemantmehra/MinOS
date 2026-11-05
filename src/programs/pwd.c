#include <stdio.h>
#include <minos.h>

int main() {
    int path[128];
    int length = get_current_directory(path);

    if (length < 0) {
        printf("pwd: could not read current directory.\n");
        return 0;
    }

    for (int index = 0; index < length; index++) {
        putchar(path[index]);
    }
    printf("\n");
    return 0;
}
