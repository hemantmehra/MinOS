#include <stdio.h>
#include <minos.h>

void draw_circle(int center_x, int center_y, int radius, int color) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distance = x * x + y * y;
            if (distance <= radius * radius &&
                distance >= radius * radius - radius * 2) {
                draw_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

void draw_triangle() {
    for (int y = 80; y <= 380; y++) {
        int half_width = (y - 80) / 2;
        for (int x = 320 - half_width; x <= 320 + half_width; x++) {
            draw_pixel(x, y, 10);
        }
    }
}

void draw_rectangle() {
    for (int y = 130; y <= 370; y++) {
        for (int x = 465; x <= 600; x++) {
            draw_pixel(x, y, 9);
        }
    }
}

int main() {
    if (create_window("Paint", 640, 480) == 0) {
        printf("paint: could not create graphics window.\n");
        return 0;
    }

    start_graphics_mode();
    clear_graphics(15);
    draw_circle(130, 240, 80, 12);
    draw_triangle();
    draw_rectangle();
    present();

    while (read_key() != 1) {
    }
    end_graphics_mode();
    return 0;
}
