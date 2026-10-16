/* Minimal software-rendered 32-bit framebuffer on top of RSX/GCM (PSL1GHT) */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <ppu-types.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include "gfx.h"
#include "font8x8.h"

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

int gfx_w, gfx_h;
static gcmContextData *ctx;
static u32 *fb[2];
static u32 fb_off[2];
static u32 *shadow;      /* drawing happens in normal RAM, copied to RSX on flip */
static int cur = 0, first = 1;

void gfx_init(void)
{
    videoState st;
    videoResolution res;
    videoConfiguration vc;
    void *host = memalign(1024 * 1024, HOST_SIZE);
    int i;

    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoGetState(0, 0, &st);
    videoGetResolution(st.displayMode.resolution, &res);

    memset(&vc, 0, sizeof(vc));
    vc.resolution = st.displayMode.resolution;
    vc.format     = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch      = res.width * sizeof(u32);
    vc.aspect     = st.displayMode.aspect;
    videoConfigure(0, &vc, NULL, 0);
    videoGetState(0, 0, &st);

    gfx_w = res.width;
    gfx_h = res.height;

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, gfx_w * gfx_h * sizeof(u32));
        rsxAddressToOffset(fb[i], &fb_off[i]);
        gcmSetDisplayBuffer(i, fb_off[i], gfx_w * sizeof(u32), gfx_w, gfx_h);
    }
    shadow = (u32 *)memalign(128, gfx_w * gfx_h * sizeof(u32));
    memset(shadow, 0, gfx_w * gfx_h * sizeof(u32));
    gcmResetFlipStatus();
}

void gfx_clear(unsigned color)
{
    int i, n = gfx_w * gfx_h;
    for (i = 0; i < n; i++) shadow[i] = color;
}

void gfx_rect(int x, int y, int w, int h, unsigned color)
{
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_w) w = gfx_w - x;
    if (y + h > gfx_h) h = gfx_h - y;
    for (yy = y; yy < y + h; yy++) {
        u32 *p = shadow + yy * gfx_w + x;
        for (xx = 0; xx < w; xx++) p[xx] = color;
    }
}

static void glyph(int x, int y, int s, unsigned color, char ch)
{
    int r, c;
    const unsigned char *g;
    if (ch < 32 || ch > 126) ch = '?';
    g = font8x8[ch - 32];
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            if ((g[r] >> c) & 1)
                gfx_rect(x + c * s, y + r * s, s, s, color);
}

void gfx_text(int x, int y, int scale, unsigned color, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int i;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (i = 0; buf[i]; i++)
        glyph(x + i * 8 * scale, y, scale, color, buf[i]);
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

void gfx_flip(void)
{
    if (first) first = 0; else wait_flip();
    memcpy(fb[cur], shadow, gfx_w * gfx_h * sizeof(u32));
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
}
