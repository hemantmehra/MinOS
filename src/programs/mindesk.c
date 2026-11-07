#include <stdio.h>
#include <minos.h>

int action(int operation, int handle, int value) {
    if (desktop_action(operation, handle, value) < 0) {
        printf("mindesk: desktop action failed.\n");
        return 0;
    }
    return 1;
}

int main() {
    int event[3];
    int info[8];
    int menu = 0;
    int drag = 0;
    int drag_owner = 0;
    int drag_dx = 0;
    int drag_dy = 0;

    if (!action(4, 0, 0)) {
        return 1;
    }
    while (1) {
        int result = desktop_next(0, event);
        if (result < 0) {
            printf("mindesk: event service failed.\n");
            return 1;
        }
        if (result == 0) {
            sleep(5);
        } else if (event[0] == 1) {
            int width = event[2] & 65535;
            int height = (event[2] >> 16) & 65535;
            if (desktop_create(event[1],
                width > 0 && width <= 640 && height > 0 && height <= 480) < 0) {
                printf("mindesk: window request no longer exists.\n");
            }
        } else if (event[0] == 4) {
            drag = 0;
        } else if (event[0] == 3 && drag != 0) {
            if (desktop_info(drag - 1, info) != 1 || info[1] != drag_owner) {
                drag = 0;
            } else {
                int x = event[1] - drag_dx;
                int y = event[2] - drag_dy;
                if (x < 0) x = 0;
                if (y < 0) y = 0;
                if (!action(3, drag, x | (y << 16))) {
                    drag = 0;
                }
            }
        } else if (event[0] == 2) {
            int x = event[1];
            int y = event[2];
            int consumed = 0;
            drag = 0;
            if (menu) {
                menu = 0;
                action(4, 0, 0);
                if (x < 136 && y >= 624 && y < 688) {
                    if (y < 656) action(1, 0, 0);
                    else action(6, 0, 0);
                    consumed = 1;
                } else if (x < 72 && y >= 688) {
                    consumed = 1;
                }
            }
            if (!consumed && y >= 688) {
                if (x < 72) {
                    menu = 1;
                    action(4, 0, 1);
                } else {
                    int count = 1;
                    for (int i = 0; i < 32; i++) {
                        if (desktop_info(i, info) == 1) count++;
                    }
                    int width = 128;
                    if (count * width > 1208) width = 1208 / count;
                    int item = (x - 72) / width;
                    if (item == 0) {
                        action(1, 0, 0);
                    } else if (item < count) {
                        int current = 0;
                        for (int i = 0; i < 32; i++) {
                            if (desktop_info(i, info) == 1) {
                                current++;
                                if (current == item) {
                                    action(2, info[0], 0);
                                    break;
                                }
                            }
                        }
                    }
                }
                consumed = 1;
            }
            if (!consumed) {
                int best = 0;
                int best_rank = 0;
                int best_owner = 0;
                int best_x = 0;
                int best_y = 0;
                int best_width = 0;
                int shell_active = 1;
                if (desktop_info(32, info) == 1) shell_active = info[0];
                if (!shell_active) {
                    for (int i = 0; i < 32; i++) {
                        if (desktop_info(i, info) == 1 && info[6] &&
                            x >= info[2] && x < info[2] + info[4] + 4 &&
                            y >= info[3] && y < info[3] + info[5] + 36 &&
                            info[7] > best_rank) {
                            best = info[0];
                            best_owner = info[1];
                            best_x = info[2];
                            best_y = info[3];
                            best_width = info[4];
                            best_rank = info[7];
                        }
                    }
                }
                if (best == 0) {
                    action(1, 0, 0);
                } else {
                    action(2, best, 0);
                    if (x >= best_x + best_width - 25 &&
                        x < best_x + best_width - 1 &&
                        y >= best_y + 5 && y < best_y + 29) {
                        action(5, best_owner, 0);
                    } else if (y >= best_y + 1 && y < best_y + 33) {
                        drag = best;
                        drag_owner = best_owner;
                        drag_dx = x - best_x;
                        drag_dy = y - best_y;
                    }
                }
            }
        }
    }
}
