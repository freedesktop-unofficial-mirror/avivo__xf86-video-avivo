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
avivo_xf86crtc_resize(ScrnInfoPtr scrn, int width, int height)
{
    scrn->virtualX = width;
    scrn->virtualY = height;
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
        cntl = 0x00010101;
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
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
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
    int div, pdiv, pmul;
    int n_pdiv, n_pmul;
    int clock;
    int diff, n_diff;

    div = 1080000 / mode->Clock;
    pdiv = 2;
    pmul = floor(((40.0 * mode->Clock * pdiv * div) / 1080000.0) + 0.5);
    clock = (pmul * 108000) / (40 * pdiv * div);
    diff = fabsl(clock - mode->Clock);
    while (1) {
        n_pdiv = pdiv + 1;
        n_pmul = floor(((40.0 * mode->Clock * n_pdiv * div) / 1080000.0)+0.5);
        clock = (n_pmul * 108000) / (40 * n_pdiv * div);
        n_diff = fabsl(clock - mode->Clock);
        if (n_diff >= diff)
            break;
        pdiv = n_pdiv;
        pmul = n_pmul;
    }
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "crtc(%d) PLL: div %d, pmul 0x%X(%d), pdiv %d\n",
               avivo_crtc->crtc_number, div, pmul, pmul, pdiv);
    OUTREG(AVIVO_PLL_CNTL, 0);
    OUTREG(AVIVO_PLL_DIVIDER, div);
    OUTREG(AVIVO_PLL_DIVIDER_CNTL, AVIVO_PLL_EN);
    OUTREG(AVIVO_PLL_POST_DIV, pdiv);
    OUTREG(AVIVO_PLL_POST_MUL, (pmul << AVIVO_PLL_POST_MUL_SHIFT));
    OUTREG(AVIVO_PLL_CNTL, AVIVO_PLL_EN);
}

static void
avivo_crtc_mode_set(xf86CrtcPtr crtc,
                   DisplayModePtr mode,
                   DisplayModePtr adjusted_mode,
                   int x, int y)
{
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;
    struct avivo_info *avivo = avivo_get_info(crtc->scrn);
    unsigned long fb_location = avivo_crtc->fb_offset + avivo->fb_addr;

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
    avivo_crtc->h_sync_pol = 0;
    avivo_crtc->v_total = adjusted_mode->CrtcVTotal - 1;
    avivo_crtc->v_blank =
        ((adjusted_mode->CrtcVTotal - adjusted_mode->CrtcVSyncStart) << 16)
        | (adjusted_mode->CrtcVTotal - adjusted_mode->CrtcVSyncStart
           + adjusted_mode->CrtcVDisplay);
    avivo_crtc->v_sync_wid = (adjusted_mode->CrtcVSyncEnd
                              - adjusted_mode->CrtcVSyncStart) << 16;
    avivo_crtc->fb_width = adjusted_mode->CrtcHDisplay;
    avivo_crtc->fb_height = adjusted_mode->CrtcVDisplay;
    avivo_crtc->fb_pitch = adjusted_mode->CrtcHDisplay;
    avivo_crtc->fb_offset = 0;
    avivo_crtc->fb_length = avivo_crtc->fb_pitch * avivo_crtc->fb_height * 4;
    switch (crtc->scrn->depth) {
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

    /* TODO: find out what this regs truely are for.
     * last guess: Switch from text to graphics mode.
     */
    OUTREG(AVIVO_VGA_MYSTERY0, 0x00010600);
    OUTREG(AVIVO_VGA_MYSTERY1, 0x00000400);

    /* setup fb format and location
     */
    OUTREG(AVIVO_CRTC1_FB_LOCATION + avivo_crtc->crtc_offset, fb_location);
    OUTREG(AVIVO_CRTC1_FB_FORMAT + avivo_crtc->crtc_offset,
           avivo_crtc->fb_format);
    OUTREG(AVIVO_CRTC1_FB_END + avivo_crtc->crtc_offset,
           fb_location + avivo_crtc->fb_length);
    OUTREG(AVIVO_CRTC1_MODE + avivo_crtc->crtc_offset, 0);
    OUTREG(AVIVO_CRTC1_60c0_MYSTERY + avivo_crtc->crtc_offset, 0);

    /* set PLL TODO: there is likely PLL registers we miss for having
     * different PLL for each CRTC for instance.
     */
    avivo_crtc_set_pll(crtc, adjusted_mode);

    /* finaly set the mode
     */
    OUTREG(AVIVO_CRTC1_FB_HEIGHT + avivo_crtc->crtc_offset,
           avivo_crtc->fb_height);
    OUTREG(AVIVO_CRTC1_EXPANSION_SOURCE + avivo_crtc->crtc_offset,
           (avivo_crtc->fb_width << 16) | avivo_crtc->fb_height);
    OUTREG(AVIVO_CRTC1_EXPANSION_CNTL + avivo_crtc->crtc_offset,
           AVIVO_CRTC_EXPANSION_EN);
    OUTREG(AVIVO_CRTC1_659C + avivo_crtc->crtc_offset, AVIVO_CRTC1_659C_VALUE);
    OUTREG(AVIVO_CRTC1_65A8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65A8_VALUE);
    OUTREG(AVIVO_CRTC1_65AC + avivo_crtc->crtc_offset, AVIVO_CRTC1_65AC_VALUE);
    OUTREG(AVIVO_CRTC1_65B8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65B8_VALUE);
    OUTREG(AVIVO_CRTC1_65BC + avivo_crtc->crtc_offset, AVIVO_CRTC1_65BC_VALUE);
    OUTREG(AVIVO_CRTC1_65C8 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65C8_VALUE);
    OUTREG(AVIVO_CRTC1_6594 + avivo_crtc->crtc_offset, AVIVO_CRTC1_6594_VALUE);
    OUTREG(AVIVO_CRTC1_65A4 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65A4_VALUE);
    OUTREG(AVIVO_CRTC1_65B0 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65B0_VALUE);
    OUTREG(AVIVO_CRTC1_65C0 + avivo_crtc->crtc_offset, AVIVO_CRTC1_65C0_VALUE);

    OUTREG(AVIVO_CRTC1_X_LENGTH + avivo_crtc->crtc_offset,
           avivo_crtc->fb_width);
    OUTREG(AVIVO_CRTC1_Y_LENGTH + avivo_crtc->crtc_offset,
           avivo_crtc->fb_height);
    OUTREG(AVIVO_CRTC1_PITCH + avivo_crtc->crtc_offset, avivo_crtc->fb_pitch);
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
avivo_crtc_destroy(xf86OutputPtr output)
{
    if (output->driver_private)
        xfree(output->driver_private);
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
    .shadow_create = NULL,
    .shadow_allocate = NULL,
    .shadow_destroy = NULL,
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
    struct avivo_info *avivo = avivo_get_info(screen_info);

    /* allocate & initialize private crtc structure */
    avivo_crtc = xcalloc (sizeof(struct avivo_crtc_private), 1);
    if (avivo_crtc == NULL)
        return FALSE;
    avivo_crtc->crtc_number = crtc_number;
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
