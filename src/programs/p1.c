#include <stdio.h>
#include <minos.h>

int main() {
    printf("P1: starting\n");

    int child = create_process("P2.EXE");
    if (child > 0) {
        printf("P1: waiting for P2\n");
        if (wait_process(child) == 1) {
            printf("P1: P2 completed\n");
        } else {
            printf("P1: could not wait for P2\n");
        }
    } else {
        printf("P1: could not start P2\n");
    }

    printf("P1: exiting\n");
    return 0;
}
