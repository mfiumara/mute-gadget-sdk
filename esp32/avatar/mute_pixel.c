// Copyright (c) the Mute Gadget SDK contributors. Apache-2.0.

/*
 * Default avatar: a round blob face. Small on purpose; drop your own
 * renderer in components/mute/avatar/mute_pixel.c to replace it.
 */

#include "mute_pixel.h"

#include <math.h>
#include <string.h>

#define W MUTE_PX_W
#define H MUTE_PX_H
#define MAP_MAX 512

enum { C_BG, C_OUT, C_BODY, C_SHADE, C_EYE, C_CHEEK, C_ACCENT, C_COUNT };

static const uint32_t ACCENTS[MUTE_MODE_COUNT] = {
    [MUTE_MODE_BOOT] = 0x7fd6ff,
    [MUTE_MODE_IDLE] = 0x7fd6ff,
    [MUTE_MODE_LISTENING] = 0x6ff0bf,
    [MUTE_MODE_THINKING] = 0xffc857,
    [MUTE_MODE_SPEAKING] = 0xa77dff,
    [MUTE_MODE_ERROR] = 0xff5c5c,
    [MUTE_MODE_OFF] = 0x404040,
};

static uint8_t s_fb[W * H];
static uint16_t s_pal[C_COUNT], s_pal_dim[C_COUNT];
static uint8_t s_map[MAP_MAX];
static int s_size = W;

static uint16_t to565(uint32_t c, float k)
{
    int r = (int)(((c >> 16) & 0xff) * k), g = (int)(((c >> 8) & 0xff) * k), b = (int)((c & 0xff) * k);
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void set_palette(mute_mode_t mode, float fade)
{
    const uint32_t colors[C_COUNT] = {
        0x000000, 0x101018, 0xf2efff, 0xc9c2e6, 0x101018, 0xff9eb5, ACCENTS[mode],
    };
    for (int i = 0; i < C_COUNT; i++) {
        s_pal[i] = to565(colors[i], fade);
        s_pal_dim[i] = to565(colors[i], fade * 0.8f);
    }
}

static void px(int x, int y, uint8_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) {
        s_fb[y * W + x] = c;
    }
}

static void disc(float cx, float cy, float r, uint8_t c)
{
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) {
                px(x, y, c);
            }
        }
    }
}

static void hline(int x0, int x1, int y, uint8_t c)
{
    for (int x = x0; x <= x1; x++) {
        px(x, y, c);
    }
}

uint32_t mute_pixel_accent(mute_mode_t mode)
{
    return mode < MUTE_MODE_COUNT ? ACCENTS[mode] : ACCENTS[MUTE_MODE_IDLE];
}

void mute_pixel_render(const mute_pose_t *p)
{
    float t = p->t, level = p->level;
    float fade = p->mode == MUTE_MODE_OFF ? fmaxf(0.0f, 1.0f - p->mode_t / 1.3f) : 1.0f;
    set_palette(p->mode, fade);
    memset(s_fb, C_BG, sizeof(s_fb));

    float bob = sinf(t * (p->mode == MUTE_MODE_SPEAKING ? 5.0f : 1.8f)) * 1.2f
              - fabsf(sinf(t * 9.0f)) * 3.0f * p->happy;
    float cx = 32.0f, cy = 32.0f + bob, r = 20.0f + level * 2.0f;

    /* Accent ring pulses with the mode, and with the voice level. */
    float ring = r + 4.0f + sinf(t * 2.0f) + level * 4.0f;
    disc(cx, cy, ring, C_ACCENT);
    disc(cx, cy, ring - 1.5f, C_BG);

    disc(cx, cy, r + 1.0f, C_OUT);
    disc(cx, cy, r, C_SHADE);
    disc(cx - 1.5f, cy - 1.5f, r - 2.0f, C_BODY);

    int ex = 7, ey = (int)(cy - 3);
    bool blink = fmodf(t, 4.0f) < 0.12f || (p->mode == MUTE_MODE_BOOT && p->mode_t < 0.9f);
    for (int s = -1; s <= 1; s += 2) {
        int x = (int)cx + s * ex;
        if (p->mode == MUTE_MODE_ERROR) {
            for (int i = -2; i <= 2; i++) {
                px(x + i, ey + i, C_EYE);
                px(x + i, ey - i, C_EYE);
            }
        } else if (blink || p->mode == MUTE_MODE_OFF || p->happy > 0.2f) {
            hline(x - 2, x + 2, ey, C_EYE);
        } else {
            disc(x + 0.5f, ey + 0.5f, p->mode == MUTE_MODE_LISTENING ? 3.0f : 2.4f, C_EYE);
        }
        disc(x + 0.5f + s * 2, ey + 5.5f, 1.8f, C_CHEEK);
    }

    int my = (int)(cy + 6);
    switch (p->mode) {
    case MUTE_MODE_SPEAKING: {
        float open = 1.0f + level * 4.0f + 0.5f * (1 + sinf(t * 22.0f));
        for (int y = 0; y < (int)open; y++) {
            hline((int)cx - 3, (int)cx + 3, my + y, C_EYE);
        }
        break;
    }
    case MUTE_MODE_LISTENING:
        disc(cx + 0.5f, my + 1.5f, 2.0f, C_EYE);
        break;
    case MUTE_MODE_THINKING:
    case MUTE_MODE_ERROR:
        hline((int)cx - 3, (int)cx + 3, my + 1, C_EYE);
        break;
    default:
        hline((int)cx - 3, (int)cx + 3, my + 1, C_EYE);
        px((int)cx - 4, my, C_EYE);
        px((int)cx + 4, my, C_EYE);
        break;
    }

    if (p->mode == MUTE_MODE_THINKING) {
        for (int i = 0; i < 3; i++) {
            if ((int)(t * 3) % 4 > i) {
                disc(cx + 14 + i * 4, cy - r - 2, 1.2f, C_ACCENT);
            }
        }
    }
}

void mute_pixel_set_size(int size)
{
    s_size = size < MAP_MAX ? size : MAP_MAX;
    bool grid = s_size >= 3 * W;
    for (int i = 0; i < s_size; i++) {
        int cell = i * W / s_size;
        bool edge = grid && (i + 1) * W / s_size != cell;
        s_map[i] = (uint8_t)(cell | (edge ? 0x80 : 0));
    }
}

void mute_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    int n = x1 - x0 + 1;
    const uint8_t *xmap = &s_map[x0];
    for (int y = y0; y <= y1; y++, dst += stride_px) {
        uint8_t m = s_map[y];
        const uint8_t *row = &s_fb[(m & 0x7f) * W];
        for (int i = 0; i < n; i++) {
            uint8_t xm = xmap[i];
            uint8_t c = row[xm & 0x7f];
            dst[i] = (xm | m) & 0x80 ? s_pal_dim[c] : s_pal[c];
        }
    }
}
