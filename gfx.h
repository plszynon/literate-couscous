#ifndef GFX_H
#define GFX_H

extern int gfx_w, gfx_h;

void gfx_init(void);
void gfx_clear(unsigned color);
void gfx_rect(int x, int y, int w, int h, unsigned color);
void gfx_text(int x, int y, int scale, unsigned color, const char *fmt, ...);
void gfx_flip(void);

#endif
