#include <stdio.h>
#include <minos.h>

/* Run once after boot, before starting Mindesk. */
int main() {
    if (create_window("Before Mindesk", 80, 80) != 0) {
        printf("desktop start test: window opened before desktop.\n");
        return 1;
    }
    if (start_desktop() != 0 || start_desktop() != 1) {
        printf("desktop start test: startup or duplicate start failed.\n");
        return 1;
    }
    if (create_window("Desktop test", 80, 80) == 0) {
        printf("desktop start test: window creation failed.\n");
        return 1;
    }
    printf("desktop start test passed.\n");
    return 0;
}
