#include <stdio.h>
#include <minos.h>

void draw_ball(int center_x, int center_y, int radius) {
    int radius_squared = radius * radius;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius_squared) {
                draw_pixel(center_x + x, center_y + y, 12);
            }
        }
    }
}

void wait_frame() {
    for (int count = 0; count < 30000; count++) {
    }
}

int main() {
    if (create_window("Bouncing Ball", 640, 480) == 0) {
        printf("ball: could not create graphics window.\n");
        return 0;
    }

    int radius = 24;
    int x = random(592) + radius;
    int y = random(432) + radius;
    int velocity_x = (random(2) * 2 - 1) * 5;
    int velocity_y = (random(2) * 2 - 1) * 5;

    while (read_key() != 1) {
        clear_graphics(15);
        draw_ball(x, y, radius);
        present();

        x += velocity_x;
        y += velocity_y;
        if (x < radius) {
            x = radius;
            velocity_x = -velocity_x;
        }
        if (x > 640 - radius) {
            x = 640 - radius;
            velocity_x = -velocity_x;
        }
        if (y < radius) {
            y = radius;
            velocity_y = -velocity_y;
        }
        if (y > 480 - radius) {
            y = 480 - radius;
            velocity_y = -velocity_y;
        }

        wait_frame();
    }
    end_graphics_mode();
    return 0;
}
