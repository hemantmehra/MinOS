#include <minos.h>

int main() {
    int radius = 140;
    int radius_squared = radius * radius;

    start_graphics_mode();
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distance = x * x + y * y;
            if (distance <= radius_squared &&
                distance >= radius_squared - radius * 2) {
                draw_pixel(640 + x, 360 + y, 12);
            }
        }
    }
    present();

    while (read_key() != 1) {
    }
    end_graphics_mode();
    return 0;
}
