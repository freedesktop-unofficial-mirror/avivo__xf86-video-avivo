/*
 * Copyright © 2007 Daniel Stone
 * Copyright © 2007 Matthew Garrett
 * Copyright © 2007 Jerome Glisse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the General Public License is included with the source
 * distribution of this driver, as COPYING.
 *
 * Authors: Daniel Stone <daniel@fooishbar.org>
 *          Matthew Garrett <mjg59@srcf.ucam.org>
 *          Jerome Glisse <glisse@freedesktop.org>
 *
 * Portions based on the Radeon and VESA drivers.
 */
/*
 * avivo cursor handling functions. 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "cursorstr.h"
#include "xf86Cursor.h"

#include "avivo.h"
#include "radeon_reg.h"

static void
avivo_cursor_show(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(AVIVO_CURSOR1_CNTL, INREG(AVIVO_CURSOR1_CNTL) | AVIVO_CURSOR_EN);
}

static void
avivo_cursor_hide(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(AVIVO_CURSOR1_CNTL, INREG(AVIVO_CURSOR1_CNTL) & ~(AVIVO_CURSOR_EN));
}

static void
avivo_cursor_set_position(ScrnInfoPtr screen_info, int x, int y)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    OUTREG(AVIVO_CURSOR1_POSITION, (x << 16) | y);

    avivo->cursor_x = x;
    avivo->cursor_y = y;
}

void
avivo_setup_cursor(struct avivo_info *avivo, int id, int enable)
{
    if (id == 1) {
        OUTREG(AVIVO_CURSOR1_CNTL, 0);

        if (enable) {
            OUTREG(AVIVO_CURSOR1_LOCATION, avivo->fb_addr +
                                           avivo->cursor_offset);
            OUTREG(AVIVO_CURSOR1_SIZE, (avivo->cursor_width << 16) |
                                       avivo->cursor_height);
            OUTREG(AVIVO_CURSOR1_CNTL, AVIVO_CURSOR_EN |
                                       (avivo->cursor_format <<
                                        AVIVO_CURSOR_FORMAT_SHIFT));
        }
    }
}


static void
avivo_cursor_load_argb(ScrnInfoPtr screen_info, CursorPtr cursor)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    CARD32 *dst = (CARD32 *)(avivo->fb_base + avivo->cursor_offset);
    CARD32 *src;
    int x, y;

    for (y = 0; y < cursor->bits->height; y++) {
        src = cursor->bits->argb + (y * cursor->bits->width);
        for (x = 0; x < cursor->bits->width; x++)
            *dst++ = *src++;
        for (x = cursor->bits->width; x < 64; x++)
            *dst++ = 0;
    }
    for (y = cursor->bits->height; y < 64; y++) {
        for (x = 0; x < 64; x++)
            *dst++ = 0;
    }

    avivo->cursor_width = cursor->bits->width;
    avivo->cursor_height = cursor->bits->height;
    avivo->cursor_format = AVIVO_CURSOR_FORMAT_ARGB;

    avivo_setup_cursor(avivo, 1, 1);
}

/* Mono ARGB cursor colours (premultiplied). */
static CARD32 mono_cursor_color[] = {
        0x00000000, /* White, fully transparent. */
        0x00000000, /* Black, fully transparent. */
        0xffffffff, /* White, fully opaque. */
        0xff000000, /* Black, fully opaque. */
};

static void
avivo_cursor_load_image(ScrnInfoPtr screen_info, unsigned char *bits)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    CARD32 *d = (CARD32 *)(avivo->fb_base + avivo->cursor_offset);
    CARD8 *s;
    CARD8 chunk;
    int i, j;

#define ARGB_PER_CHUNK  (8 * sizeof (chunk) / 2)
    s = bits;
    for (i = 0; i < (64 * 64 / ARGB_PER_CHUNK); i++) {
        chunk = *s++;
        for (j = 0; j < ARGB_PER_CHUNK; j++, chunk >>= 2)
            *d++ = mono_cursor_color[chunk & 3];
    }

    avivo->cursor_bg = mono_cursor_color[2];
    avivo->cursor_fg = mono_cursor_color[3];
    avivo->cursor_width = 63;
    avivo->cursor_height = 63;
    avivo->cursor_format = AVIVO_CURSOR_FORMAT_ARGB;

    avivo_setup_cursor(avivo, 1, 1);
}

static void
avivo_cursor_set_colors(ScrnInfoPtr screen_info, int bg, int fg)
{
    /* FIXME implement */ ;
}

void
avivo_cursor_init(ScreenPtr screen)
{
    ScrnInfoPtr screen_info = xf86Screens[screen->myNum];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    xf86CursorInfoPtr cursor;

    cursor = xcalloc(1, sizeof(*cursor));
    if (!cursor)
        FatalError("Couldn't create cursor info\n");

    cursor->MaxWidth = 64;
    cursor->MaxHeight = 64;
    cursor->Flags = (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
                     HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
                     HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1);
    cursor->SetCursorPosition = avivo_cursor_set_position;
    cursor->LoadCursorARGB = avivo_cursor_load_argb;
    cursor->LoadCursorImage = avivo_cursor_load_image;
    cursor->SetCursorColors = avivo_cursor_set_colors;
    cursor->HideCursor = avivo_cursor_hide;
    cursor->ShowCursor = avivo_cursor_show;

    if (!xf86InitCursor(screen, cursor))
        FatalError("Couldn't initialise cursor\n");
}
