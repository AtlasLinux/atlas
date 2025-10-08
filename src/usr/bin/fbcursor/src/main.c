#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include "fb.h"

#define CUR_W 10
#define CUR_H 10

static volatile int running = 1;
static void handle_sigint(int sig) { (void)sig; running = 0; }

int main(void)
{
    signal(SIGINT, handle_sigint);

    fb_t fb = fb_init();
    if (fb_open(&fb, "/dev/fb0") < 0) {
        perror("fb_open");
        return 1;
    }

    int mfd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    if (mfd < 0) {
        perror("open /dev/input/mice");
        fb_close(&fb);
        return 1;
    }

    int x = fb.width / 2, y = fb.height / 2;
    int px = x, py = y;
    unsigned char buttons = 0;

    const color_t bg = COLOR_BLACK;
    const color_t cur = COLOR_WHITE;
    const color_t cur_pressed = COLOR_GREEN;

    /* full clear once */
    fb_clear(&fb, bg);

    while (running) {
        unsigned char data[3];
        ssize_t n = read(mfd, data, sizeof(data));
        if (n == 3) {
            buttons = data[0];
            x += (int8_t)data[1];
            y -= (int8_t)data[2];
        }

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > (int)fb.width - CUR_W)  x = fb.width - CUR_W;
        if (y > (int)fb.height - CUR_H) y = fb.height - CUR_H;

        if (x != px || y != py) {
            fb_fillrect(&fb, px, py, CUR_W, CUR_H, bg);  /* erase old */
            color_t c = (buttons & 1) ? cur_pressed : cur;
            fb_fillrect(&fb, x, y, CUR_W, CUR_H, c);     /* draw new */
            px = x; py = y;
        }

        // usleep(16000);
    }

    close(mfd);
    fb_close(&fb);
    return 0;
}
