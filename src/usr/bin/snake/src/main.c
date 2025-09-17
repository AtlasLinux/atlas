#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <string.h>
#include <sys/select.h>
#include "fb.h"

#define CELL_SIZE 10
#define INITIAL_LENGTH 5
#define BORDER 1

typedef enum { UP, DOWN, LEFT, RIGHT } dir_t;

typedef struct { int x, y; } point_t;

typedef struct {
    point_t *body;
    int length;
    int capacity;
    dir_t dir;
} snake_t;

/* Terminal input setup */
static struct termios orig_term;
static void reset_terminal(void) { tcsetattr(STDIN_FILENO, TCSANOW, &orig_term); }
static void init_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &orig_term) == -1) { perror("tcgetattr"); exit(1); }
    atexit(reset_terminal);
    struct termios newt = orig_term;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0; newt.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == -1) { perror("tcsetattr"); exit(1); }
}
static int kbhit(void) {
    struct timeval tv = {0,0}; fd_set fds;
    FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv) > 0;
}
static char getch(void) { char c = 0; if (read(STDIN_FILENO, &c, 1) <= 0) return 0; return c; }

/* Back-buffer helper */
static uint8_t *use_backbuf_if_present(fb_t *fb) {
    if (fb->backbuf) { uint8_t *o = fb->fbmem; fb->fbmem = fb->backbuf; return o; }
    return NULL;
}
static void restore_fbmem_if_switched(fb_t *fb, uint8_t *orig) { if (orig) fb->fbmem = orig; }

/* spawn food not on snake and not on border */
static void spawn_food(point_t *food, const snake_t *snake, int grid_w, int grid_h) {
    int ok;
    int interior_w = grid_w - 2*BORDER;
    int interior_h = grid_h - 2*BORDER;
    if (interior_w <= 0 || interior_h <= 0) { food->x = BORDER; food->y = BORDER; return; }
    do {
        ok = 1;
        food->x = (rand() % interior_w) + BORDER;
        food->y = (rand() % interior_h) + BORDER;
        for (int i = 0; i < snake->length; ++i)
            if (snake->body[i].x == food->x && snake->body[i].y == food->y) { ok = 0; break; }
    } while (!ok);
}

/* shift body one step (drops last cell) */
static void move_snake(snake_t *snake) {
    for (int i = snake->length - 1; i > 0; --i) snake->body[i] = snake->body[i-1];
    switch (snake->dir) {
    case UP:    snake->body[0].y--; break;
    case DOWN:  snake->body[0].y++; break;
    case LEFT:  snake->body[0].x--; break;
    case RIGHT: snake->body[0].x++; break;
    }
}

/* collision test including border */
static int check_collision(const snake_t *snake, int grid_w, int grid_h) {
    int x = snake->body[0].x, y = snake->body[0].y;
    /* border cells are walls: valid range is [BORDER .. grid_w-1-BORDER] */
    if (x < BORDER || x > grid_w - BORDER - 1 || y < BORDER || y > grid_h - BORDER - 1) return 1;
    for (int i = 1; i < snake->length; ++i) if (snake->body[i].x == x && snake->body[i].y == y) return 1;
    return 0;
}

/* draw helpers */
static void draw_cell(fb_t *fb, int gx, int gy, color_t c) {
    unsigned px = (unsigned)(gx * CELL_SIZE), py = (unsigned)(gy * CELL_SIZE);
    fb_fillrect(fb, px, py, CELL_SIZE, CELL_SIZE, c);
}

/* draw the border on the current drawing buffer */
static void draw_border_on_buffer(fb_t *fb, int grid_w, int grid_h) {
    /* top/bottom rows */
    for (int x = 0; x < grid_w; ++x) {
        draw_cell(fb, x, 0, COLOR_WHITE);
        draw_cell(fb, x, grid_h - 1, COLOR_WHITE);
    }
    /* left/right columns */
    for (int y = 1; y < grid_h - 1; ++y) {
        draw_cell(fb, 0, y, COLOR_WHITE);
        draw_cell(fb, grid_w - 1, y, COLOR_WHITE);
    }
}

/* Full initial draw: clears back buffer (if present) and renders border, snake, food */
static void draw_full_scene(fb_t *fb, const snake_t *snake, const point_t *food, int grid_w, int grid_h) {
    uint8_t *orig = use_backbuf_if_present(fb);

    if (fb->backbuf) memset(fb->fbmem, 0, fb->screensize);
    else fb_clear(fb, COLOR_BLACK);

    draw_border_on_buffer(fb, grid_w, grid_h);

    for (int i = 0; i < snake->length; ++i) draw_cell(fb, snake->body[i].x, snake->body[i].y, COLOR_GREEN);

    draw_cell(fb, food->x, food->y, COLOR_RED);

    restore_fbmem_if_switched(fb, orig);
    fb_flip(fb);
}

/* Incremental update: clear old tail (unless ate) but never erase a border cell */
static void draw_incremental(fb_t *fb, const point_t *old_tail, const snake_t *snake, const point_t *food, int ate, int grid_w, int grid_h) {
    uint8_t *orig = use_backbuf_if_present(fb);

    /* Erase old tail only if we didn't grow and it's not on the border */
    if (!ate) {
        if (!(old_tail->x < BORDER || old_tail->x > grid_w - BORDER - 1 ||
              old_tail->y < BORDER || old_tail->y > grid_h - BORDER - 1)) {
            draw_cell(fb, old_tail->x, old_tail->y, COLOR_BLACK);
        }
    }

    /* Draw new head */
    draw_cell(fb, snake->body[0].x, snake->body[0].y, COLOR_GREEN);

    /* Draw food */
    draw_cell(fb, food->x, food->y, COLOR_RED);

    restore_fbmem_if_switched(fb, orig);
    fb_flip(fb);
}

/* --- main --- */
int main(void) {
    srand((unsigned)time(NULL));
    init_terminal();

    fb_t fb = fb_init();
    if (fb_open(&fb, "/dev/fb0") != 0) { fprintf(stderr, "Failed to open framebuffer\n"); return 1; }

    /* Grid determined from framebuffer */
    const int grid_w = fb.width / CELL_SIZE;
    const int grid_h = fb.height / CELL_SIZE;
    /* need at least space for two border rows + some interior for the snake */
    if (grid_w <= 4 || grid_h <= 4) {
        fprintf(stderr, "Framebuffer too small for CELL_SIZE=%d (got %dx%d grid)\n", CELL_SIZE, grid_w, grid_h);
        fb_close(&fb);
        return 1;
    }

    /* Snake allocation */
    snake_t snake;
    snake.capacity = grid_w * grid_h;
    snake.length = INITIAL_LENGTH;
    snake.dir = RIGHT;
    snake.body = malloc((size_t)snake.capacity * sizeof(point_t));
    if (!snake.body) { perror("malloc"); fb_close(&fb); return 1; }

    /* Initialize snake head inside interior so tail doesn't touch border:
       choose head_x centrally but at least INITIAL_LENGTH and at most grid_w-2 */
    int head_x = (grid_w - 1) / 2;
    if (head_x < INITIAL_LENGTH) head_x = INITIAL_LENGTH;
    if (head_x > grid_w - BORDER - 1) head_x = grid_w - BORDER - 1;
    int midy = grid_h / 2;
    if (midy < BORDER) midy = BORDER;
    if (midy > grid_h - BORDER - 1) midy = grid_h - BORDER - 1;

    for (int i = 0; i < snake.length; ++i) {
        /* tail behind head in LEFT direction of array so head points RIGHT */
        snake.body[i].x = head_x - i;
        snake.body[i].y = midy;
    }

    /* Food */
    point_t food;
    spawn_food(&food, &snake, grid_w, grid_h);

    /* Initial full draw */
    draw_full_scene(&fb, &snake, &food, grid_w, grid_h);

    int running = 1;
    const useconds_t tick_us = 100000 / 3; /* 30 FPS */
    while (running) {
        if (kbhit()) {
            char c = getch();
            switch (c) {
            case 'w': if (snake.dir != DOWN) snake.dir = UP; break;
            case 's': if (snake.dir != UP) snake.dir = DOWN; break;
            case 'a': if (snake.dir != RIGHT) snake.dir = LEFT; break;
            case 'd': if (snake.dir != LEFT) snake.dir = RIGHT; break;
            case 'q': running = 0; break;
            default: break;
            }
        }

        /* Save true old tail (before move). If we grow, we will append this. */
        point_t old_tail = snake.body[snake.length - 1];

        move_snake(&snake);

        if (check_collision(&snake, grid_w, grid_h)) break;

        /* Did we eat? If yes, append old_tail (so tail stays in place). */
        int ate = 0;
        if (snake.body[0].x == food.x && snake.body[0].y == food.y) {
            if (snake.length < snake.capacity) {
                snake.body[snake.length] = old_tail; /* append the pre-move tail */
                snake.length++;
            }
            ate = 1;
            spawn_food(&food, &snake, grid_w, grid_h);
        }

        draw_incremental(&fb, &old_tail, &snake, &food, ate, grid_w, grid_h);
        usleep(tick_us);
    }

    fb_close(&fb);
    reset_terminal();
    printf("Game Over! Score: %d\n", snake.length - INITIAL_LENGTH);
    free(snake.body);
    return 0;
}
