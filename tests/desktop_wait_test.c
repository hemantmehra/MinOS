#include <stdio.h>
#include <minos.h>

/* Run with argument "mindesk" and the deliberately sleeping test server. */
int main() {
    int child = create_process("A:/Programs/DSWAIT.EXE");
    if (child < 0) {
        printf("desktop wait test: could not start child.\n");
        return 1;
    }
    sleep(100);
    if (kill_process() != 1) {
        printf("desktop wait test: could not stop server.\n");
        return 1;
    }
    if (wait_process(child) != 0) {
        printf("desktop wait test: pending request did not fail.\n");
        return 1;
    }
    printf("desktop wait test passed.\n");
    return 0;
}
