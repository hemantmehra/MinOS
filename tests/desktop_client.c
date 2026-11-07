#include <stdio.h>
#include <minos.h>

int main() {
    int event[3];
    int info[8];
    if (desktop_next(0, event) != -1 ||
        desktop_info(0, info) != -1 ||
        desktop_create(1, 1) != -1 ||
        desktop_action(1, 0, 0) != -1) {
        printf("desktop test: server access was not restricted.\n");
        return 1;
    }
    if (create_window("Invalid size", 641, 1) != 0) {
        printf("desktop test: invalid window accepted.\n");
        return 1;
    }
    if (create_window("Desktop test", 120, 80) == 0) {
        printf("desktop test: window creation failed.\n");
        return 1;
    }
    clear_graphics(12);
    present();
    sleep(100);
    printf("desktop client tests passed.\n");
    return 0;
}
