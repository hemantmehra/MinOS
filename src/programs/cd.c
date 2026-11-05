#include <stdio.h>
#include <minos.h>

int main() {
    if (change_directory() == 0) {
        printf("cd: could not change directory.\n");
    }
    return 0;
}
