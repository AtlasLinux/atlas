#include "fb.h"
#include <unistd.h>

int main(void) {
    fb_t fb = fb_init();

    /* draw into backbuffer */
    fb_clear(&fb, COLOR_BLACK);
    fb_draw_string(&fb, 0, 0, "Frame 1", COLOR_WHITE, COLOR_BLACK);

    fb_flip(&fb); /* push to screen */
    sleep(2);

    /* draw next frame */
    fb_clear(&fb, COLOR_BLACK);
    fb_draw_string(&fb, 0, 0, "Frame 2", COLOR_GREEN, COLOR_BLACK);

    fb_flip(&fb);
    sleep(2);

    fb_close(&fb);
    return 0;
}
