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
 */
/*
 * avivo crtc handling functions. 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
/* DPMS */
#define DPMS_SERVER
#include <X11/extensions/dpms.h>

#include "avivo.h"
#include "radeon_reg.h"

static Bool
avivo_xf86crtc_resize(ScrnInfoPtr screen_info, int width, int height)
{
    screen_info->virtualX = width;
    screen_info->virtualY = height;
    return TRUE;
}

static const xf86CrtcConfigFuncsRec avivo_xf86crtc_config_funcs = {
    avivo_xf86crtc_resize
};

static void
avivo_crtc_enable(xf86CrtcPtr crtc, int enable)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    int scan_enable, cntl;

    if (enable) {
        scan_enable = AVIVO_CRTC_SCAN_EN;
        cntl = AVIVO_CRTC_EN;
    } else {
        scan_enable = 0;
        cntl = 0;
    }
    
    OUTREG(AVIVO_CRTC1_SCAN_ENABLE + avivo_crtc->crtc_offset, scan_enable);
    OUTREG(AVIVO_CRTC1_CNTL + avivo_crtc->crtc_offset, cntl);
    avivo_wait_idle(avivo);
}

static void
avivo_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    switch (mode) {
    case DPMSModeOn:
    case DPMSModeStandby:
    case DPMSModeSuspend:
        avivo_crtc_enable(crtc, 1);
        break;
    case DPMSModeOff:
        avivo_crtc_enable(crtc, 0);
        break;
    }
}

static Bool
avivo_crtc_lock(xf86CrtcPtr crtc)
{
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);

    /* wait idle */
    avivo_wait_idle(avivo);

    /* return false as long we have no DRI things to do */
    return FALSE;
}

static void
avivo_crtc_unlock(xf86CrtcPtr crtc)
{
    /* nothing to do as long as we have no DRI */
}

static Bool
avivo_crtc_mode_fixup(xf86CrtcPtr crtc,
                      DisplayModePtr mode,
                      DisplayModePtr adjusted_mode)
{
    /* maybe we should do some sanity check here */
    return TRUE;
}

static void
avivo_crtc_prepare(xf86CrtcPtr crtc)
{
    /* turn off this crtc should i do anythings more ? */
    crtc->funcs->dpms(crtc, DPMSModeOff);
}

static void
avivo_crtc_set_pll(xf86CrtcPtr crtc, DisplayModePtr mode)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
	double c;
	int div1, div2, clock;
	int sdiv1, sdiv2, smul, sclock;
	int mul;

	c = (((double)mode->Clock) * 40.0) / 1080000.0;
	clock = mode->Clock;
	sdiv1 = 0;
	sdiv2 = 0;
	smul  = 0;
    for (div1 = 2; div1 <= 6; div1++) {
        for (div2 = div1 + 1; div2 < (div1 + 14); div2++) {
            mul = ceil(c * div1 * div2);
            clock = (mul * 1080000) / (40 * div1 * div2);
            if ((div1 * div2) > 20 && (clock - mode->Clock) >= 0 &&
                fabsl(sclock - mode->Clock) >= fabsl(clock - mode->Clock) &&
                mul < 256 && (div1 * div2) > (sdiv1 * sdiv2)) {
                sdiv1  = div1;
                sdiv2  = div2;
                smul   = mul;
                sclock = clock;
            }
        }
    }
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "crtc(%d) Clock: mode %d, PLL %d\n",
               avivo_crtc->crtc_number, mode->Clock, sclock);
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "crtc(%d) PLL  : div %d, pmul 0x%X(%d), pdiv %d\n",
               avivo_crtc->crtc_number, sdiv1, smul, smul, sdiv2);
    switch (avivo_crtc->crtc_number) {
    case 0:
        OUTREG(AVIVO_PLL1_POST_DIV_CNTL, AVIVO_PLL_POST_DIV_EN);
        OUTREG(AVIVO_PLL1_POST_DIV_MYSTERY, AVIVO_PLL_POST_DIV_MYSTERY_VALUE);
        OUTREG(AVIVO_PLL1_POST_DIV, sdiv1);
        OUTREG(AVIVO_PLL1_POST_MUL, (smul << AVIVO_PLL_POST_MUL_SHIFT));
        OUTREG(AVIVO_PLL1_DIVIDER_CNTL, AVIVO_PLL_DIVIDER_EN);
        OUTREG(AVIVO_PLL1_DIVIDER, sdiv2);
        OUTREG(AVIVO_PLL1_MYSTERY0, AVIVO_PLL_MYSTERY0_VALUE);
        OUTREG(AVIVO_PLL1_MYSTERY1, AVIVO_PLL_MYSTERY1_VALUE);
        break;
    case 1:
        OUTREG(AVIVO_PLL2_POST_DIV_CNTL, AVIVO_PLL_POST_DIV_EN);
        OUTREG(AVIVO_PLL2_POST_DIV_MYSTERY, AVIVO_PLL_POST_DIV_MYSTERY_VALUE);
        OUTREG(AVIVO_PLL2_POST_DIV, sdiv1);
        OUTREG(AVIVO_PLL2_POST_MUL, (smul << AVIVO_PLL_POST_MUL_SHIFT));
        OUTREG(AVIVO_PLL2_DIVIDER_CNTL, AVIVO_PLL_DIVIDER_EN);
        OUTREG(AVIVO_PLL2_DIVIDER, sdiv2);
        OUTREG(AVIVO_PLL2_MYSTERY0, AVIVO_PLL_MYSTERY0_VALUE);
        OUTREG(AVIVO_PLL2_MYSTERY1, AVIVO_PLL_MYSTERY1_VALUE);
        break;
    }
    OUTREG(AVIVO_CRTC_PLL_SOURCE, (0 << AVIVO_CRTC1_PLL_SOURCE_SHIFT)	
                                  | (1 << AVIVO_CRTC2_PLL_SOURCE_SHIFT));
    OUTREG(0x454, INREG(0x454) | 0x2);
    avivo_wait_idle(avivo);
}

static void
avivo_crtc_mode_set(xf86CrtcPtr crtc,
                   DisplayModePtr mode,
                   DisplayModePtr adjusted_mode,
                   int x, int y)
{
    ScrnInfoPtr screen_info = crtc->scrn;
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    unsigned long fb_location = avivo_crtc->fb_offset + avivo->fb_addr;
    int regval;

    /* compute mode value
     * TODO: hsync & vsync pol likely not handled properly
     */
    avivo_crtc->h_total = adjusted_mode->HTotal - 1;
    avivo_crtc->h_blank =
        ((adjusted_mode->CrtcHTotal - adjusted_mode->CrtcHSyncStart) << 16)
        | (adjusted_mode->CrtcHTotal - adjusted_mode->CrtcHSyncStart
           + adjusted_mode->CrtcHDisplay);
    avivo_crtc->h_sync_wid = (adjusted_mode->CrtcHSyncEnd
                              - adjusted_mode->CrtcHSyncStart) << 16;
    avivo_crtc->h_sync_pol = (adjusted_mode->Flags & V_NHSYNC) ? 1 : 0;
    avivo_crtc->v_total = adjusted_mode->CrtcVTotal - 1;
    avivo_crtc->v_blank =
        ((adjusted_mode->CrtcVTotal - adjusted_mode->CrtcVSyncStart) << 16)
        | (adjusted_mode->CrtcVTotal - adjusted_mode->CrtcVSyncStart
           + adjusted_mode->CrtcVDisplay);
    avivo_crtc->v_sync_wid = (adjusted_mode->CrtcVSyncEnd
                              - adjusted_mode->CrtcVSyncStart) << 16;
    avivo_crtc->v_sync_pol = (adjusted_mode->Flags & V_NVSYNC) ? 1 : 0;
    avivo_crtc->fb_width = adjusted_mode->CrtcHDisplay;
    avivo_crtc->fb_height = screen_info->virtualY;
    avivo_crtc->fb_pitch = adjusted_mode->CrtcHDisplay;
    avivo_crtc->fb_offset = 0;
    avivo_crtc->fb_length = avivo_crtc->fb_pitch * avivo_crtc->fb_height * 4;
    switch (crtc->scrn->bitsPerPixel) {
    case 15:
        avivo_crtc->fb_format = AVIVO_CRTC_FORMAT_ARGB15;
        break;
    case 16:
        avivo_crtc->fb_format = AVIVO_CRTC_FORMAT_ARGB16;
        break;
    case 24:
    case 32:
        avivo_crtc->fb_format = AVIVO_CRTC_FORMAT_ARGB32;
        break;
    default:
        FatalError("Unsupported screen depth: %d\n", xf86GetDepth());
    }
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "crtc(%d) hdisp %d, htotal %d, hss %d, hse %d, hsk %d, hsp %d\n",
               avivo_crtc->crtc_number, adjusted_mode->CrtcHDisplay,
               adjusted_mode->CrtcHTotal, adjusted_mode->CrtcHSyncStart,
               adjusted_mode->CrtcHSyncEnd, adjusted_mode->CrtcHSkew,
               avivo_crtc->h_sync_pol);
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "crtc(%d) vdisp %d, vtotal %d, vss %d, vse %d, vsc %d, vsp %d\n",
               avivo_crtc->crtc_number, adjusted_mode->CrtcVDisplay,
               adjusted_mode->CrtcVTotal, adjusted_mode->CrtcVSyncStart,
               adjusted_mode->CrtcVSyncEnd, adjusted_mode->VScan,
               avivo_crtc->v_sync_pol);
    /* TODO: find out what this regs truely are for.
     * last guess: Switch from text to graphics mode.
     */

    regval = (AVIVO_VGA1_CONTROL_SYNC_POLARITY_SELECT | AVIVO_VGA1_CONTROL_OVERSCAN_TIMING_SELECT | AVIVO_VGA1_CONTROL_OVERSCAN_COLOR_EN);
    OUTREG(AVIVO_VGA1_CONTROL, regval);
    regval = (AVIVO_VGA2_CONTROL_OVERSCAN_TIMING_SELECT);
    OUTREG(AVIVO_VGA2_CONTROL, regval);

    /* setup fb format and location
     */
    OUTREG(AVIVO_CRTC1_FB_LOCATION + avivo_crtc->crtc_offset, fb_location);
    OUTREG(AVIVO_CRTC1_FB_FORMAT + avivo_crtc->crtc_offset,
           avivo_crtc->fb_format);
    OUTREG(AVIVO_CRTC1_FB_END + avivo_crtc->crtc_offset,
           fb_location + avivo_crtc->fb_length);
    OUTREG(AVIVO_CRTC1_MODE + avivo_crtc->crtc_offset, 0);
    /* avivo can only shift offset by 4 pixel in x if you program somethings
     * not multiple of 4 you gonna drive the GPU crazy and likely won't
     * be able to restore it without cold reboot (vbe post not enough)
     */
    x = x & ~3;
    OUTREG(AVIVO_CRTC1_OFFSET_END + avivo_crtc->crtc_offset,
           ((mode->HDisplay + x -128) << 16) | (mode->VDisplay + y - 128));
    OUTREG(AVIVO_CRTC1_OFFSET_START + avivo_crtc->crtc_offset, (x << 16) | y);

    avivo_crtc_set_pll(crtc, adjusted_mode);

    /* finaly set the mode
     */
    OUTREG(AVIVO_CRTC1_FB_HEIGHT + avivo_crtc->crtc_offset,
           avivo_crtc->fb_height);
    OUTREG(AVIVO_CRTC1_EXPANSION_SOURCE + avivo_crtc->crtc_offset,
           (mode->HDisplay << 16) | mode->VDisplay);
    OUTREG(AVIVO_CRTC1_EXPANSION_CNTL + avivo_crtc->crtc_offset,
           AVIVO_CRTC_EXPANSION_EN);
    OUTREG(AVIVO_CRTC1_6594 + avivo_crtc->crtc_offset, AVIVO_CRTC1_6594_VALUE);
    OUTREG(AVIVO_CRTC1_659C + avivo_crtc->crtc_offset, AVIVO_CRTC1_659C_VALUE);
    OUTREG(AVIVO_CRTC1_65A8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65A8_VALUE);
    OUTREG(AVIVO_CRTC1_65AC + avivo_crtc->crtc_offset, AVIVO_CRTC1_65AC_VALUE);
    OUTREG(AVIVO_CRTC1_65B8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65B8_VALUE);
    OUTREG(AVIVO_CRTC1_65BC + avivo_crtc->crtc_offset, AVIVO_CRTC1_65BC_VALUE);
    OUTREG(AVIVO_CRTC1_65C8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65C8_VALUE);
    OUTREG(AVIVO_CRTC1_65A4 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65A4_VALUE);
    OUTREG(AVIVO_CRTC1_65B0 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65B0_VALUE);
    OUTREG(AVIVO_CRTC1_65C0 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65C0_VALUE);

    OUTREG(AVIVO_CRTC1_X_LENGTH + avivo_crtc->crtc_offset,
           crtc->scrn->virtualX);
    OUTREG(AVIVO_CRTC1_Y_LENGTH + avivo_crtc->crtc_offset,
           crtc->scrn->virtualY);
    OUTREG(AVIVO_CRTC1_PITCH + avivo_crtc->crtc_offset,
           crtc->scrn->displayWidth);
    OUTREG(AVIVO_CRTC1_H_TOTAL + avivo_crtc->crtc_offset, avivo_crtc->h_total);
    OUTREG(AVIVO_CRTC1_H_BLANK + avivo_crtc->crtc_offset, avivo_crtc->h_blank);
    OUTREG(AVIVO_CRTC1_H_SYNC_WID + avivo_crtc->crtc_offset,
           avivo_crtc->h_sync_wid);
    OUTREG(AVIVO_CRTC1_H_SYNC_POL + avivo_crtc->crtc_offset,
           avivo_crtc->h_sync_pol);
    OUTREG(AVIVO_CRTC1_V_TOTAL + avivo_crtc->crtc_offset, avivo_crtc->v_total);
    OUTREG(AVIVO_CRTC1_V_BLANK + avivo_crtc->crtc_offset, avivo_crtc->v_blank);
    OUTREG(AVIVO_CRTC1_V_SYNC_WID + avivo_crtc->crtc_offset,
           avivo_crtc->v_sync_wid);
    OUTREG(AVIVO_CRTC1_V_SYNC_POL + avivo_crtc->crtc_offset,
           avivo_crtc->v_sync_pol);
}

static void
avivo_crtc_commit(xf86CrtcPtr crtc)
{
    crtc->funcs->dpms (crtc, DPMSModeOn);
    if (crtc->scrn->pScreen != NULL)
        xf86_reload_cursors(crtc->scrn->pScreen);
}

static void *
avivo_crtc_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
    ScrnInfoPtr screen_info = crtc->scrn;
    ScreenPtr screen = screen_info->pScreen;
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    unsigned long pitch;
    unsigned long offset;
    int align, size;

    /* The XFree86 linear allocator operates in units of screen pixels. */
    align = 256;
    pitch = screen_info->displayWidth * avivo->bpp;
    size = pitch * height;
    size = (size + avivo->bpp - 1) / avivo->bpp;
    align = (align + avivo->bpp - 1) / avivo->bpp;

    assert(avivo_crtc->fb_rotate == NULL);
    avivo_crtc->fb_rotate = avivo_xf86AllocateOffscreenLinear(screen, size,
                                                              align, NULL,
                                                              NULL, NULL);
    if (avivo_crtc->fb_rotate == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow memory for rotated CRTC\n");
        return NULL;
    }
    offset = avivo_crtc->fb_offset + avivo_crtc->fb_rotate->offset * avivo->bpp;
    return avivo->fb_base + offset;
}

static PixmapPtr
avivo_crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
    ScrnInfoPtr screen_info = crtc->scrn;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    unsigned long pitch;
    PixmapPtr pixmap;

    if (!data)
        data = avivo_crtc_shadow_allocate(crtc, width, height);
                            
    pitch = screen_info->displayWidth * avivo->bpp;
    pixmap = GetScratchPixmapHeader(screen_info->pScreen,
                                    width, height,
                                    screen_info->depth,
                                    screen_info->bitsPerPixel,
                                    pitch,
                                    data);
    if (pixmap == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
    }
    return pixmap;
}

static void
avivo_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr pixmap, void *data)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;

    if (pixmap)
        FreeScratchPixmapHeader(pixmap);

    if (data) {
        xf86FreeOffscreenLinear(avivo_crtc->fb_rotate);
        avivo_crtc->fb_rotate = NULL;
    }
}

static void
avivo_crtc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{
    /* TODO: implement */
}

static void
avivo_crtc_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    OUTREG(AVIVO_CURSOR1_POSITION + avivo_crtc->crtc_offset, (x << 16) | y);
    avivo_crtc->cursor_x = x;
    avivo_crtc->cursor_y = y;
}

static void
avivo_crtc_show_cursor(xf86CrtcPtr crtc)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);

    OUTREG(AVIVO_CURSOR1_CNTL + avivo_crtc->crtc_offset,
           INREG(AVIVO_CURSOR1_CNTL + avivo_crtc->crtc_offset)
           | AVIVO_CURSOR_EN);
}

static void
avivo_crtc_hide_cursor(xf86CrtcPtr crtc)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);

    OUTREG(AVIVO_CURSOR1_CNTL+ avivo_crtc->crtc_offset,
           INREG(AVIVO_CURSOR1_CNTL + avivo_crtc->crtc_offset)
           & ~(AVIVO_CURSOR_EN));
}

static void
avivo_crtc_cursor_load_argb(xf86CrtcPtr crtc, CARD32 *image)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    CARD32 *dst = (CARD32 *)(avivo->fb_base + avivo_crtc->cursor_offset);

    memcpy(dst, image, 64 * 64 * 4);
}

static void
avivo_crtc_destroy(xf86CrtcPtr crtc)
{
    if (crtc->driver_private)
        xfree(crtc->driver_private);
}

static const xf86CrtcFuncsRec avivo_crtc_funcs = {
    .dpms = avivo_crtc_dpms,
    .save = NULL, /* this got saved elsewhere */
    .restore = NULL, /* this got restored elsewhere */
    .lock = avivo_crtc_lock,
    .unlock = avivo_crtc_unlock,
    .mode_fixup = avivo_crtc_mode_fixup,
    .prepare = avivo_crtc_prepare,
    .mode_set = avivo_crtc_mode_set,
    .commit = avivo_crtc_commit,
    .gamma_set = NULL,
    .shadow_create = avivo_crtc_shadow_create,
    .shadow_allocate = avivo_crtc_shadow_allocate,
    .shadow_destroy = avivo_crtc_shadow_destroy,
    .set_cursor_colors = avivo_crtc_set_cursor_colors,
    .set_cursor_position = avivo_crtc_set_cursor_position,
    .show_cursor = avivo_crtc_show_cursor,
    .hide_cursor = avivo_crtc_hide_cursor,
    .load_cursor_image = NULL,
    .load_cursor_argb = avivo_crtc_cursor_load_argb,
    .destroy = avivo_crtc_destroy,
};

static Bool
avivo_crtc_init(ScrnInfoPtr screen_info, int crtc_number)
{
    xf86CrtcPtr crtc;
    struct avivo_crtc_private *avivo_crtc;

    /* allocate & initialize private crtc structure */
    avivo_crtc = xcalloc (sizeof(struct avivo_crtc_private), 1);
    if (avivo_crtc == NULL)
        return FALSE;
    avivo_crtc->fb_rotate = NULL;
    avivo_crtc->crtc_number = crtc_number;
    avivo_crtc->fb_offset = 0;
    avivo_crtc->cursor_offset = 0;
    avivo_crtc->crtc_offset = 0;
    if (avivo_crtc->crtc_number == 1)
        avivo_crtc->crtc_offset = AVIVO_CRTC2_H_TOTAL - AVIVO_CRTC1_H_TOTAL;

    /* allocate & initialize xf86Crtc */
    crtc = xf86CrtcCreate (screen_info, &avivo_crtc_funcs);
    if (crtc == NULL) {
        xfree(avivo_crtc);
        return FALSE;
    }

    crtc->driver_private = avivo_crtc;
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "added CRTC %d\n", crtc_number);
    return TRUE;
}

Bool
avivo_crtc_create(ScrnInfoPtr screen_info)
{
    xf86CrtcConfigPtr xf86_crtc_config;

    /* allocate crtc config and register crtc config function ie the resize
     * function which is a dummy function as i believe it should get call
     * with value higher than those set with xf86CrtcSetSizeRange
     */
    xf86CrtcConfigInit(screen_info, &avivo_xf86crtc_config_funcs);
    xf86CrtcSetSizeRange(screen_info, 320, 200, 2048, 2048);

    xf86_crtc_config = XF86_CRTC_CONFIG_PTR(screen_info);

    /*
     * add both crtc i think all r5xx chipset got two crtc
     */
    if (!avivo_crtc_init(screen_info, 0))
        return FALSE;
    if (!avivo_crtc_init(screen_info, 1))
        return FALSE;
    return TRUE;
}
