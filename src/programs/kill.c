#include <stdio.h>
#include <minos.h>

int main() {
    int result = kill_process();

    if (result < 0) {
        printf("Usage: kill <process-name>\n");
    } else if (result == 0) {
        printf("No matching process is running.\n");
    } else {
        printf("Terminated process.\n");
    }
    return 0;
}
