#include <stdio.h>
#include <minos.h>

struct Game {
    int *bricks;
    int paddle;
    int x;
    int y;
    int dx;
    int dy;
    int left;
    int right;
    int extended;
    int score;
    int lives;
    int remaining;
    int state;
};

void rectangle(int x, int y, int width, int height, int color) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            draw_pixel(x + col, y + row, color);
        }
    }
}

void label(int x, int y, char *pixels, int color) {
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (pixels[row * 5 + col] == '1') {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void score_digits(int value) {
    for (int digit = 0; digit < 3; digit++) {
        int number = value % 10;
        value /= 10;
        int x = 70 - digit * 12;
        if (number != 1 && number != 4) rectangle(x + 2, 7, 5, 2, 15);
        if (number != 5 && number != 6) rectangle(x + 7, 9, 2, 5, 15);
        if (number != 2) rectangle(x + 7, 16, 2, 5, 15);
        if (number != 1 && number != 4 && number != 7) rectangle(x + 2, 21, 5, 2, 15);
        if (number == 0 || number == 2 || number == 6 || number == 8) rectangle(x, 16, 2, 5, 15);
        if (number != 1 && number != 2 && number != 3 && number != 7) rectangle(x, 9, 2, 5, 15);
        if (number != 0 && number != 1 && number != 7) rectangle(x + 2, 14, 5, 2, 15);
    }
}

void reset_ball(struct Game *game) {
    game->paddle = 136;
    game->x = 158;
    game->y = 190;
    game->dx = 2;
    game->dy = -2;
    game->left = 0;
    game->right = 0;
}

void reset_game(struct Game *game) {
    for (int i = 0; i < 32; i++) game->bricks[i] = 1;
    game->score = 0;
    game->lives = 3;
    game->remaining = 32;
    game->state = 0;
    game->extended = 0;
    reset_ball(game);
}

int input(struct Game *game, int code) {
    if (code == 0xE0) {
        game->extended = 1;
        return 0;
    }
    if (game->extended) {
        game->extended = 0;
        if (code == 0x4B) {
            if (!game->left && game->state == 0) game->paddle -= 5;
            game->left = 1;
        }
        if (code == 0xCB) game->left = 0;
        if (code == 0x4D) {
            if (!game->right && game->state == 0) game->paddle += 5;
            game->right = 1;
        }
        if (code == 0xCD) game->right = 0;
        if (game->paddle < 0) game->paddle = 0;
        if (game->paddle > 272) game->paddle = 272;
        return 0;
    }
    if (code == 1) return 1;
    if (code == 0x13 && game->state != 0) reset_game(game);
    return 0;
}

int hit_brick(struct Game *game, int x, int y) {
    for (int i = 0; i < 32; i++) {
        if (game->bricks[i]) {
            int bx = 16 + (i % 8) * 36;
            int by = 36 + (i / 8) * 14;
            if (x + 5 > bx && x < bx + 32 &&
                y + 5 > by && y < by + 10) {
                game->bricks[i] = 0;
                game->score += 10;
                game->remaining--;
                if (game->remaining == 0) game->state = 1;
                return 1;
            }
        }
    }
    return 0;
}

void update(struct Game *game) {
    if (game->state != 0) return;
    if (game->left && !game->right) game->paddle -= 5;
    if (game->right && !game->left) game->paddle += 5;
    if (game->paddle < 0) game->paddle = 0;
    if (game->paddle > 272) game->paddle = 272;

    for (int step = 0; step < 2; step++) {
        int nx = game->x + game->dx / 2;
        if (nx < 0 || nx > 315 || hit_brick(game, nx, game->y)) {
            game->dx = -game->dx;
        } else {
            game->x = nx;
        }
        int ny = game->y + game->dy / 2;
        if (ny < 28 || hit_brick(game, game->x, ny)) {
            game->dy = -game->dy;
        } else if (game->dy > 0 && game->y + 5 <= 218 &&
                   ny + 5 >= 218 && game->x + 5 > game->paddle &&
                   game->x < game->paddle + 48) {
            game->dy = -2;
            if (game->x + 2 < game->paddle + 24) game->dx = -2;
            else game->dx = 2;
        } else {
            game->y = ny;
        }
        if (game->state != 0) return;
        if (game->y >= 240) {
            game->lives--;
            if (game->lives == 0) game->state = 2;
            else reset_ball(game);
            return;
        }
    }
}

void paint(struct Game *game, int buffer, int *drawn, int *old_x,
           int *old_y, int *old_paddle, int *old_score, int *old_lives,
           int *old_state) {
    if (old_x[buffer] >= 0) {
        rectangle(old_x[buffer], old_y[buffer], 5, 5, 0);
        rectangle(old_paddle[buffer], 218, 48, 5, 0);
    }
    for (int i = 0; i < 32; i++) {
        int slot = buffer * 32 + i;
        if (drawn[slot] != game->bricks[i]) {
            int color = 0;
            if (game->bricks[i]) color = 12 - i / 8;
            rectangle(16 + (i % 8) * 36, 36 + (i / 8) * 14, 32, 10, color);
            drawn[slot] = game->bricks[i];
        }
    }
    if (old_score[buffer] != game->score) {
        rectangle(46, 7, 33, 16, 0);
        score_digits(game->score);
        old_score[buffer] = game->score;
    }
    if (old_lives[buffer] != game->lives) {
        rectangle(270, 8, 39, 8, 0);
        for (int i = 0; i < game->lives; i++) rectangle(270 + i * 13, 8, 8, 8, 12);
        old_lives[buffer] = game->lives;
    }
    if (old_state[buffer] != game->state) {
        rectangle(105, 120, 110, 30, 0);
        if (game->state == 1) {
            label(132, 130, "10001100011010110101101011101110001", 10);
            label(140, 130, "01110100011000110001100011000101110", 10);
            label(148, 130, "10001110011010110011100011000110001", 10);
        } else if (game->state == 2) {
            label(125, 125, "01111100001000010111100011000101111", 12);
            label(132, 125, "01110100011000111111100011000110001", 12);
            label(139, 125, "10001110111010110101100011000110001", 12);
            label(146, 125, "11111100001000011110100001000011111", 12);
            label(160, 125, "01110100011000110001100011000101110", 12);
            label(167, 125, "10001100011000110001100010101000100", 12);
            label(174, 125, "11111100001000011110100001000011111", 12);
            label(181, 125, "11110100011000111110101001001010001", 12);
        }
        if (game->state != 0) {
            label(140, 140, "11110100011000111110101001001010001", 15);
            label(151, 140, "01111100001000001110000010000111110", 15);
            label(158, 140, "11111001000010000100001000010000100", 15);
            label(165, 140, "01110100011000111111100011000110001", 15);
            label(172, 140, "11110100011000111110101001001010001", 15);
            label(179, 140, "11111001000010000100001000010000100", 15);
        }
        old_state[buffer] = game->state;
    }
    if (game->state == 0) {
        rectangle(game->paddle, 218, 48, 5, 15);
        rectangle(game->x, game->y, 5, 5, 14);
        old_x[buffer] = game->x;
        old_y[buffer] = game->y;
        old_paddle[buffer] = game->paddle;
    } else {
        old_x[buffer] = -1;
    }
}

int main() {
    struct Game game;
    int bricks[32];
    int drawn[64];
    int old_x[2];
    int old_y[2];
    int old_paddle[2];
    int old_score[2];
    int old_lives[2];
    int old_state[2];

    if (create_window("Breakout", 320, 240) == 0) {
        printf("breakout: could not create graphics window.\n");
        return 1;
    }
    game.bricks = bricks;
    reset_game(&game);
    for (int i = 0; i < 64; i++) drawn[i] = -1;
    for (int i = 0; i < 2; i++) {
        clear_graphics(0);
        label(8, 10, "01111100001000001110000010000111110", 15);
        label(15, 10, "01111100001000010000100001000001111", 15);
        label(22, 10, "01110100011000110001100011000101110", 15);
        label(29, 10, "11110100011000111110101001001010001", 15);
        label(36, 10, "11111100001000011110100001000011111", 15);
        old_x[i] = -1;
        old_score[i] = -1;
        old_lives[i] = -1;
        old_state[i] = -1;
        paint(&game, i, drawn, old_x, old_y, old_paddle, old_score, old_lives, old_state);
        present();
    }
    int buffer = 0;
    while (1) {
        int code = read_key();
        while (code != 0) {
            if (input(&game, code)) return 0;
            code = read_key();
        }
        update(&game);
        paint(&game, buffer, drawn, old_x, old_y, old_paddle, old_score, old_lives, old_state);
        present();
        buffer = 1 - buffer;
        sleep(16);
    }
}
