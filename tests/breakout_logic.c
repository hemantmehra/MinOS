#define main breakout_program_main
#include "../src/programs/breakout.c"
#undef main

void draw_pixel(int x, int y, int color) { (void)x; (void)y; (void)color; }
void clear_graphics(int color) { (void)color; }
void present(void) {}
void sleep(int milliseconds) { (void)milliseconds; }
int read_key(void) { return 0; }
int create_window(const char *title, int width, int height) {
    (void)title; (void)width; (void)height; return 0;
}

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d\n", __LINE__); return 1; } } while (0)

int main(void) {
    struct Game game;
    int bricks[32];
    game.bricks = bricks;
    reset_game(&game);
    CHECK(game.lives == 3 && game.remaining == 32 && game.score == 0);
    input(&game, 0xE0); input(&game, 0x4B);
    CHECK(game.paddle == 131 && game.left);
    update(&game);
    CHECK(game.paddle == 126);
    input(&game, 0xE0); input(&game, 0xCB);
    CHECK(!game.left);
    input(&game, 0xE0); input(&game, 0x4D);
    input(&game, 0xE0); input(&game, 0xCD);
    CHECK(game.paddle == 131 && !game.right);
    game.paddle = 0;
    input(&game, 0xE0); input(&game, 0x4B);
    CHECK(game.paddle == 0);
    input(&game, 0xE0); input(&game, 0xCB);
    CHECK(hit_brick(&game, 16, 36) == 1);
    CHECK(game.score == 10 && game.remaining == 31 && !bricks[0]);
    CHECK(hit_brick(&game, 16, 36) == 0);
    game.x = 0; game.y = 160; game.dx = -2; game.dy = -2;
    update(&game);
    CHECK(game.dx == 2 && game.x >= 0);
    game.paddle = 100; game.x = 110; game.y = 213; game.dx = 2; game.dy = 2;
    update(&game);
    CHECK(game.dy == -2 && game.dx == -2);
    game.x = 100; game.y = 239; game.dy = 2;
    update(&game);
    CHECK(game.lives == 2 && game.state == 0 && game.y == 190);
    game.lives = 1; game.x = 100; game.y = 239; game.dy = 2;
    update(&game);
    CHECK(game.lives == 0 && game.state == 2);
    input(&game, 0x13);
    CHECK(game.lives == 3 && game.score == 0 && game.remaining == 32);
    for (int i = 0; i < 31; i++) game.bricks[i] = 0;
    game.remaining = 1;
    CHECK(hit_brick(&game, 16 + 7 * 36, 36 + 3 * 14) == 1);
    CHECK(game.state == 1 && game.score == 10);
    CHECK(input(&game, 1) == 1);
    printf("Breakout logic tests passed\n");
    return 0;
}
