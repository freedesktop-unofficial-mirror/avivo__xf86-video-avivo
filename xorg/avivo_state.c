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
 * avivo state handling functions. 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef WITH_VGAHW
#include "vgaHW.h"
#endif

#include "avivo.h"
#include "radeon_reg.h"

void
avivo_wait_idle(struct avivo_info *avivo)
{
    int i = 1000;
  
    while (--i && INREG(0x6494) != 0x3fffffff);

    if (!i)
        FatalError("Avivo: chip lockup!\n");
}

void
avivo_save_cursor(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    state->cursor1_cntl = INREG(AVIVO_CURSOR1_CNTL);
    state->cursor1_location = INREG(AVIVO_CURSOR1_LOCATION);
    state->cursor1_size = INREG(AVIVO_CURSOR1_SIZE);
    state->cursor1_position = INREG(AVIVO_CURSOR1_POSITION);
}

void
avivo_restore_cursor(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    OUTREG(AVIVO_CURSOR1_CNTL, state->cursor1_cntl);
    OUTREG(AVIVO_CURSOR1_LOCATION, state->cursor1_location);
    OUTREG(AVIVO_CURSOR1_SIZE, state->cursor1_size);
    OUTREG(AVIVO_CURSOR1_POSITION, state->cursor1_position);
}

void
avivo_restore_state(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    avivo_set_mc(screen_info, AVIVO_MC_MEMORY_MAP, state->mc_memory_map);
    OUTREG(AVIVO_VGA_MEMORY_BASE, state->vga_memory_base);
    OUTREG(AVIVO_VGA_FB_START, state->vga_fb_start);
    OUTREG(AVIVO_VGA1_CONTROL, state->vga_mystery0);
    OUTREG(AVIVO_VGA2_CONTROL, state->vga_mystery1);

    OUTREG(AVIVO_PLL1_POST_DIV_CNTL, state->pll1_post_div_cntl);
    OUTREG(AVIVO_PLL1_POST_DIV, state->pll1_post_div);
    OUTREG(AVIVO_PLL1_POST_DIV_MYSTERY, state->pll1_post_div_mystery);
    OUTREG(AVIVO_PLL1_POST_MUL, state->pll1_post_mul);
    OUTREG(AVIVO_PLL1_DIVIDER_CNTL, state->pll1_divider_cntl);
    OUTREG(AVIVO_PLL1_DIVIDER, state->pll1_divider);
    OUTREG(AVIVO_PLL1_MYSTERY0, state->pll1_mystery0);
    OUTREG(AVIVO_PLL1_MYSTERY1, state->pll1_mystery1);
    OUTREG(AVIVO_PLL2_POST_DIV_CNTL, state->pll2_post_div_cntl);
    OUTREG(AVIVO_PLL2_POST_DIV, state->pll2_post_div);
    OUTREG(AVIVO_PLL2_POST_DIV_MYSTERY, state->pll2_post_div_mystery);
    OUTREG(AVIVO_PLL2_POST_MUL, state->pll2_post_mul);
    OUTREG(AVIVO_PLL2_DIVIDER_CNTL, state->pll2_divider_cntl);
    OUTREG(AVIVO_PLL2_DIVIDER, state->pll2_divider);
    OUTREG(AVIVO_PLL2_MYSTERY0, state->pll2_mystery0);
    OUTREG(AVIVO_PLL2_MYSTERY1, state->pll2_mystery1);
    OUTREG(AVIVO_CRTC_PLL_SOURCE, state->crtc_pll_source);

    OUTREG(AVIVO_CRTC1_H_TOTAL, state->crtc1_h_total);
    OUTREG(AVIVO_CRTC1_H_BLANK, state->crtc1_h_blank);
    OUTREG(AVIVO_CRTC1_H_SYNC_WID, state->crtc1_h_sync_wid);
    OUTREG(AVIVO_CRTC1_H_SYNC_POL, state->crtc1_h_sync_pol);
    OUTREG(AVIVO_CRTC1_V_TOTAL, state->crtc1_v_total);
    OUTREG(AVIVO_CRTC1_V_BLANK, state->crtc1_v_blank);
    /*
     * Weird we shouldn't restore sync width when going back to text
     * mode, it must not be a 0 value, i guess a deeper look in cold
     * text mode register value would help to understand what is
     * truely needed to do.
     */
#if 0
    OUTREG(AVIVO_CRTC1_V_SYNC_WID, state->crtc1_v_sync_wid);
#endif
    OUTREG(AVIVO_CRTC1_V_SYNC_POL, state->crtc1_v_sync_pol);
    OUTREG(AVIVO_CRTC1_CNTL, state->crtc1_cntl);
    OUTREG(AVIVO_CRTC1_MODE, state->crtc1_mode);
    OUTREG(AVIVO_CRTC1_60c0_MYSTERY, state->crtc1_60c0_mystery);
    OUTREG(AVIVO_CRTC1_SCAN_ENABLE, state->crtc1_scan_enable);
    OUTREG(AVIVO_CRTC1_FB_FORMAT, state->crtc1_fb_format);
    OUTREG(AVIVO_CRTC1_FB_LOCATION, state->crtc1_fb_location);
    OUTREG(AVIVO_CRTC1_FB_END, state->crtc1_fb_end);
    OUTREG(AVIVO_CRTC1_PITCH, state->crtc1_pitch);
    OUTREG(AVIVO_CRTC1_X_LENGTH, state->crtc1_x_length);
    OUTREG(AVIVO_CRTC1_Y_LENGTH, state->crtc1_y_length);
    OUTREG(AVIVO_CRTC1_FB_HEIGHT, state->crtc1_fb_height);
    OUTREG(AVIVO_CRTC1_OFFSET_START, state->crtc1_offset_start);
    OUTREG(AVIVO_CRTC1_OFFSET_END, state->crtc1_offset_end);
    OUTREG(AVIVO_CRTC1_EXPANSION_SOURCE, state->crtc1_expn_size);
    OUTREG(AVIVO_CRTC1_EXPANSION_CNTL, state->crtc1_expn_cntl);
    OUTREG(AVIVO_CRTC1_6594, state->crtc1_6594);
    OUTREG(AVIVO_CRTC1_659C, state->crtc1_659c);
    OUTREG(AVIVO_CRTC1_65A4, state->crtc1_65a4);
    OUTREG(AVIVO_CRTC1_65A8, state->crtc1_65a8);
    OUTREG(AVIVO_CRTC1_65AC, state->crtc1_65ac);
    OUTREG(AVIVO_CRTC1_65B0, state->crtc1_65b0);
    OUTREG(AVIVO_CRTC1_65B8, state->crtc1_65b8);
    OUTREG(AVIVO_CRTC1_65BC, state->crtc1_65bc);
    OUTREG(AVIVO_CRTC1_65C0, state->crtc1_65c0);
    OUTREG(AVIVO_CRTC1_65C8, state->crtc1_65c8);
    OUTREG(AVIVO_CRTC2_H_TOTAL, state->crtc2_h_total);
    OUTREG(AVIVO_CRTC2_H_BLANK, state->crtc2_h_blank);
#if 0
    OUTREG(AVIVO_CRTC2_H_SYNC_WID, state->crtc2_h_sync_wid);
#endif
    OUTREG(AVIVO_CRTC2_H_SYNC_POL, state->crtc2_h_sync_pol);
    OUTREG(AVIVO_CRTC2_V_TOTAL, state->crtc2_v_total);
    OUTREG(AVIVO_CRTC2_V_BLANK, state->crtc2_v_blank);
    OUTREG(AVIVO_CRTC2_V_SYNC_WID, state->crtc2_v_sync_wid);
    OUTREG(AVIVO_CRTC2_V_SYNC_POL, state->crtc2_v_sync_pol);
    OUTREG(AVIVO_CRTC2_CNTL, state->crtc2_cntl);
    OUTREG(AVIVO_CRTC2_MODE, state->crtc2_mode);
    OUTREG(AVIVO_CRTC2_SCAN_ENABLE, state->crtc2_scan_enable);
    OUTREG(AVIVO_CRTC2_FB_FORMAT, state->crtc2_fb_format);
    OUTREG(AVIVO_CRTC2_FB_LOCATION, state->crtc2_fb_location);
    OUTREG(AVIVO_CRTC2_FB_END, state->crtc2_fb_end);
    OUTREG(AVIVO_CRTC2_PITCH, state->crtc2_pitch);
    OUTREG(AVIVO_CRTC2_X_LENGTH, state->crtc2_x_length);
    OUTREG(AVIVO_CRTC2_Y_LENGTH, state->crtc2_y_length);

    OUTREG(AVIVO_DAC1_CNTL, state->dac1_cntl);
    OUTREG(AVIVO_DAC1_MYSTERY1, state->dac1_mystery1);
    OUTREG(AVIVO_DAC1_MYSTERY2, state->dac1_mystery2);
    OUTREG(AVIVO_TMDSA_CNTL, state->tmds1_cntl);
    OUTREG(AVIVO_TMDSA_BIT_DEPTH_CONTROL, state->tmds1_mystery1);
    OUTREG(AVIVO_TMDSA_DATA_SYNCHRONIZATION, state->tmds1_mystery2);
    OUTREG(AVIVO_TMDSA_CLOCK_CNTL, state->tmds1_clock_cntl);
    OUTREG(AVIVO_TMDSA_TRANSMITTER_CONTROL, state->tmds1_mystery3);
    OUTREG(AVIVO_DAC2_CNTL, state->dac2_cntl);
    OUTREG(AVIVO_DAC2_MYSTERY1, state->dac2_mystery1);
    OUTREG(AVIVO_DAC2_MYSTERY2, state->dac2_mystery2);
    OUTREG(AVIVO_LVTMA_CNTL, state->tmds2_cntl);
    OUTREG(AVIVO_LVTMA_BIT_DEPTH_CONTROL, state->tmds2_mystery1);
    OUTREG(AVIVO_LVTMA_DATA_SYNCHRONIZATION, state->tmds2_mystery2);
    OUTREG(AVIVO_LVTMA_CLOCK_CNTL, state->tmds2_clock_cntl);
    OUTREG(AVIVO_LVTMA_TRANSMITTER_CONTROL, state->tmds2_mystery3);
#ifdef WITH_VGAHW
    vgaHWPtr hwp = VGAHWPTR(screen_info);
    vgaHWUnlock(hwp);
    vgaHWRestore(screen_info, &hwp->SavedReg, VGA_SR_MODE | VGA_SR_FONTS );
    vgaHWLock(hwp);
#endif

    avivo_restore_cursor(screen_info);
}    

void
avivo_save_state(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;    

#ifdef WITH_VGAHW
    vgaHWPtr hwp = VGAHWPTR(screen_info);
    vgaHWUnlock(hwp);
    vgaHWSave(screen_info, &hwp->SavedReg, VGA_SR_MODE | VGA_SR_FONTS);
    vgaHWLock(hwp);
#endif

    avivo_save_cursor(screen_info);

    state->mc_memory_map = avivo_get_mc(screen_info, AVIVO_MC_MEMORY_MAP);
    state->vga_memory_base = INREG(AVIVO_VGA_MEMORY_BASE);
    state->vga_fb_start = INREG(AVIVO_VGA_FB_START);
    state->vga_mystery0 = INREG(AVIVO_VGA1_CONTROL);
    state->vga_mystery1 = INREG(AVIVO_VGA2_CONTROL);

    state->pll1_post_div_cntl = INREG(AVIVO_PLL1_POST_DIV_CNTL);
    state->pll1_post_div = INREG(AVIVO_PLL1_POST_DIV);
    state->pll1_post_div_mystery = INREG(AVIVO_PLL1_POST_DIV_MYSTERY);
    state->pll1_post_mul = INREG(AVIVO_PLL1_POST_MUL);
    state->pll1_divider_cntl = INREG(AVIVO_PLL1_DIVIDER_CNTL);
    state->pll1_divider = INREG(AVIVO_PLL1_DIVIDER);
    state->pll1_mystery0 = INREG(AVIVO_PLL1_MYSTERY0);
    state->pll1_mystery1 = INREG(AVIVO_PLL1_MYSTERY1);
    state->pll2_post_div_cntl = INREG(AVIVO_PLL2_POST_DIV_CNTL);
    state->pll2_post_div = INREG(AVIVO_PLL2_POST_DIV);
    state->pll2_post_div_mystery = INREG(AVIVO_PLL2_POST_DIV_MYSTERY);
    state->pll2_post_mul = INREG(AVIVO_PLL2_POST_MUL);
    state->pll2_divider_cntl = INREG(AVIVO_PLL2_DIVIDER_CNTL);
    state->pll2_divider = INREG(AVIVO_PLL2_DIVIDER);
    state->pll2_mystery0 = INREG(AVIVO_PLL2_MYSTERY0);
    state->pll2_mystery1 = INREG(AVIVO_PLL2_MYSTERY1);
    state->crtc_pll_source = INREG(AVIVO_CRTC_PLL_SOURCE);

    state->crtc1_h_total = INREG(AVIVO_CRTC1_H_TOTAL);
    state->crtc1_h_blank = INREG(AVIVO_CRTC1_H_BLANK);
    state->crtc1_h_sync_wid = INREG(AVIVO_CRTC1_H_SYNC_WID);
    state->crtc1_h_sync_pol = INREG(AVIVO_CRTC1_H_SYNC_POL);
    state->crtc1_v_total = INREG(AVIVO_CRTC1_V_TOTAL);
    state->crtc1_v_blank = INREG(AVIVO_CRTC1_V_BLANK);
    state->crtc1_v_sync_wid = INREG(AVIVO_CRTC1_V_SYNC_WID);
    state->crtc1_v_sync_pol = INREG(AVIVO_CRTC1_V_SYNC_POL);
    state->crtc1_cntl = INREG(AVIVO_CRTC1_CNTL);
    state->crtc1_mode = INREG(AVIVO_CRTC1_MODE);
    state->crtc1_60c0_mystery = INREG(AVIVO_CRTC1_60c0_MYSTERY);
    state->crtc1_scan_enable = INREG(AVIVO_CRTC1_SCAN_ENABLE);
    state->crtc1_fb_format = INREG(AVIVO_CRTC1_FB_FORMAT);
    state->crtc1_fb_location = INREG(AVIVO_CRTC1_FB_LOCATION);
    state->crtc1_fb_end = INREG(AVIVO_CRTC1_FB_END);
    state->crtc1_pitch = INREG(AVIVO_CRTC1_PITCH);
    state->crtc1_x_length = INREG(AVIVO_CRTC1_X_LENGTH);
    state->crtc1_y_length = INREG(AVIVO_CRTC1_Y_LENGTH);
    state->crtc1_fb_height = INREG(AVIVO_CRTC1_FB_HEIGHT);
    state->crtc1_offset_start = INREG(AVIVO_CRTC1_OFFSET_START);
    state->crtc1_offset_end = INREG(AVIVO_CRTC1_OFFSET_END);
    state->crtc1_expn_size = INREG(AVIVO_CRTC1_EXPANSION_SOURCE);
    state->crtc1_expn_cntl = INREG(AVIVO_CRTC1_EXPANSION_CNTL);
    state->crtc1_6594 = INREG(AVIVO_CRTC1_6594);
    state->crtc1_659c = INREG(AVIVO_CRTC1_659C);
    state->crtc1_65a4 = INREG(AVIVO_CRTC1_65A4);
    state->crtc1_65a8 = INREG(AVIVO_CRTC1_65A8);
    state->crtc1_65ac = INREG(AVIVO_CRTC1_65AC);
    state->crtc1_65b0 = INREG(AVIVO_CRTC1_65B0);
    state->crtc1_65b8 = INREG(AVIVO_CRTC1_65B8);
    state->crtc1_65bc = INREG(AVIVO_CRTC1_65BC);
    state->crtc1_65c0 = INREG(AVIVO_CRTC1_65C0);
    state->crtc1_65c8 = INREG(AVIVO_CRTC1_65C8);

    state->crtc2_h_total = INREG(AVIVO_CRTC2_H_TOTAL);
    state->crtc2_h_blank = INREG(AVIVO_CRTC2_H_BLANK);
    state->crtc2_h_sync_wid = INREG(AVIVO_CRTC2_H_SYNC_WID);
    state->crtc2_h_sync_pol = INREG(AVIVO_CRTC2_H_SYNC_POL);
    state->crtc2_v_total = INREG(AVIVO_CRTC2_V_TOTAL);
    state->crtc2_v_blank = INREG(AVIVO_CRTC2_V_BLANK);
    state->crtc2_v_sync_wid = INREG(AVIVO_CRTC2_V_SYNC_WID);
    state->crtc2_v_sync_pol = INREG(AVIVO_CRTC2_V_SYNC_POL);
    state->crtc2_cntl = INREG(AVIVO_CRTC2_CNTL);
    state->crtc2_mode = INREG(AVIVO_CRTC2_MODE);
    state->crtc2_scan_enable = INREG(AVIVO_CRTC2_SCAN_ENABLE);
    state->crtc2_fb_format = INREG(AVIVO_CRTC2_FB_FORMAT);
    state->crtc2_fb_location = INREG(AVIVO_CRTC2_FB_LOCATION);
    state->crtc2_fb_end = INREG(AVIVO_CRTC2_FB_END);
    state->crtc2_pitch = INREG(AVIVO_CRTC2_PITCH);
    state->crtc2_x_length = INREG(AVIVO_CRTC2_X_LENGTH);
    state->crtc2_y_length = INREG(AVIVO_CRTC2_Y_LENGTH);

    state->dac1_cntl = INREG(AVIVO_DAC1_CNTL);
    state->dac1_mystery1 = INREG(AVIVO_DAC1_MYSTERY1);
    state->dac1_mystery2 = INREG(AVIVO_DAC1_MYSTERY2);

    state->tmds1_cntl = INREG(AVIVO_TMDSA_CNTL);
    state->tmds1_mystery1 = INREG(AVIVO_TMDSA_BIT_DEPTH_CONTROL);
    state->tmds1_mystery2 = INREG(AVIVO_TMDSA_DATA_SYNCHRONIZATION);
    state->tmds1_clock_cntl = INREG(AVIVO_TMDSA_CLOCK_CNTL);
    state->tmds1_mystery3 = INREG(AVIVO_TMDSA_TRANSMITTER_CONTROL);

    state->dac2_cntl = INREG(AVIVO_DAC2_CNTL);
    state->dac2_mystery1 = INREG(AVIVO_DAC2_MYSTERY1);
    state->dac2_mystery2 = INREG(AVIVO_DAC2_MYSTERY2);

    state->tmds2_cntl = INREG(AVIVO_LVTMA_CNTL);
    state->tmds2_mystery1 = INREG(AVIVO_LVTMA_BIT_DEPTH_CONTROL);
    state->tmds2_mystery2 = INREG(AVIVO_LVTMA_DATA_SYNCHRONIZATION);
    state->tmds2_clock_cntl = INREG(AVIVO_LVTMA_CLOCK_CNTL);
    state->tmds2_mystery3 = INREG(AVIVO_LVTMA_TRANSMITTER_CONTROL);
}
