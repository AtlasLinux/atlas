#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    uint8_t *fbmem;       /* mapped framebuffer */
    size_t screensize;
    unsigned width;
    unsigned height;
    unsigned bpp;
    unsigned line_length;

    /* optional software back buffer */
    uint8_t *backbuf;
} fb_t;

/* Color abstraction */
typedef struct {
    uint8_t r, g, b;
} color_t;

#define COLOR(r,g,b) ((color_t){(r),(g),(b)})
#define COLOR_BLACK  COLOR(0,0,0)
#define COLOR_WHITE  COLOR(255,255,255)
#define COLOR_RED    COLOR(255,0,0)
#define COLOR_GREEN  COLOR(0,255,0)
#define COLOR_BLUE   COLOR(0,0,255)

fb_t fb_init(void);
int  fb_open(fb_t *fb, const char *dev);
void fb_close(fb_t *fb);

/* Drawing primitives */
void fb_clear(const fb_t *fb, color_t c);
void fb_putpixel(const fb_t *fb, unsigned x, unsigned y, color_t c);
void fb_hline(const fb_t *fb, unsigned x, unsigned y, unsigned w, color_t c);
void fb_vline(const fb_t *fb, unsigned x, unsigned y, unsigned h, color_t c);
void fb_fillrect(const fb_t *fb, unsigned x, unsigned y, unsigned w, unsigned h, color_t c);
void fb_rect(const fb_t *fb, unsigned x, unsigned y, unsigned w, unsigned h, color_t c);
void fb_line(const fb_t *fb, int x0, int y0, int x1, int y1, color_t c);
void fb_draw_char(const fb_t *fb, unsigned x, unsigned y,
                  char ch, color_t fg, color_t bg);
void fb_draw_string(const fb_t *fb, unsigned x, unsigned y,
                    const char *str, color_t fg, color_t bg);
void fb_flip(const fb_t* fb);

#endif
