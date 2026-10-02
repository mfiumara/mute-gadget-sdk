/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mute_state.h"

/*
 * Procedural pixel-art renderer for the Mute character.
 *
 * Mute is drawn on a coarse MUTE_PX_W x MUTE_PX_H grid with a small palette,
 * ordered dithering and hard outlines, then blown up to the screen with
 * nearest-neighbour blocks so the pixels stay chunky.
 */

#define MUTE_PX_W 64
#define MUTE_PX_H 64

typedef struct {
    mute_mode_t mode;
    float t;         /* seconds since boot */
    float mode_t;    /* seconds in current mode */
    float level;     /* 0..1 live audio level */
    float happy;     /* 0..1 pet reaction */
} mute_pose_t;

/* Accent colour of a mode (for the surrounding UI), as 0xRRGGBB. */
uint32_t mute_pixel_accent(mute_mode_t mode);

/* Render one frame into Mute's own MUTE_PX_W x MUTE_PX_H grid. */
void mute_pixel_render(const mute_pose_t *pose);

/* Size (square, in screen pixels) mute_pixel_scale() blows the grid up to. */
void mute_pixel_set_size(int px);

/*
 * Write screen pixels [x0, x1] x [y0, y1] of the blown-up frame as RGB565,
 * stride_px apart. Cheap enough to call per display strip, so the full-size
 * image never has to exist in RAM.
 */
void mute_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
