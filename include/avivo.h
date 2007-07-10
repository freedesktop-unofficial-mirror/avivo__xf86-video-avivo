/*
 * Copyright © 2007 Daniel Stone
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
 * Author: Daniel Stone <daniel@fooishbar.org>
 *         Matthew Garrett <mjg59@srcf.ucam.org>
 *         Jerome Glisse <glisse@freedesktop.org>
 */

#ifndef _AVIVO_H_
#define _AVIVO_H_

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86str.h"
#include "xf86i2c.h"
#include "xf86DDC.h"
#include "xf86Crtc.h"
#include "xf86_OSproc.h"
#include "xf86Resources.h"
#include "xf86RAC.h"
#include "xf86fbman.h"
#include "compiler.h"
#include "fb.h"

#include "avivo_chipset.h"

#ifdef PCIACCESS
#include <pciaccess.h>
#endif

#define AVIVO_NAME              "avivo"
#define AVIVO_DRIVER_NAME       "avivo"
#define AVIVO_DRIVER_VERSION \
    ((PACKAGE_VERSION_MAJOR << 20) | \
     (PACKAGE_VERSION_MINOR << 10) | \
     (PACKAGE_VERSION_PATCHLEVEL))

#define RADEON_VBIOS_SIZE 0x00010000

#define INREG(x) MMIO_IN32(avivo->ctrl_base, x)
#define OUTREG(x, y) MMIO_OUT32(avivo->ctrl_base, x, y)

struct avivo_crtc_private {
    FBLinearPtr       fb_rotate;
    int               crtc_number;
    unsigned long     crtc_offset;
    INT16             cursor_x;
    INT16             cursor_y;
    unsigned long     cursor_offset;
    unsigned long     fb_offset;
    int               h_total, h_blank, h_sync_wid, h_sync_pol;
    int               v_total, v_blank, v_sync_wid, v_sync_pol;
    int               fb_format, fb_length;
    int               fb_pitch, fb_width, fb_height;
};

struct avivo_output_private {
    xf86ConnectorType type;
    I2CBusPtr         i2c;
    unsigned long     output_offset;
    int               number;
    char              *name;
};

struct avivo_state
{
    int mc_memory_map;
    int vga_memory_base;
    int vga_fb_start;
    int vga_mystery0;
    int vga_mystery1;
    int pll1_post_div_cntl;
    int pll1_post_div;
    int pll1_post_div_mystery;
    int pll1_post_mul;
    int pll1_divider_cntl;
    int pll1_divider;
    int pll1_mystery0;
    int pll1_mystery1;
    int pll2_post_div_cntl;
    int pll2_post_div;
    int pll2_post_div_mystery;
    int pll2_post_mul;
    int pll2_divider_cntl;
    int pll2_divider;
    int pll2_mystery0;
    int pll2_mystery1;
    int crtc_pll_source;
    int crtc1_h_total;
    int crtc1_h_blank;
    int crtc1_h_sync_wid;
    int crtc1_h_sync_pol;
    int crtc1_v_total;
    int crtc1_v_blank;
    int crtc1_v_sync_wid;
    int crtc1_v_sync_pol;
    int crtc1_cntl;
    int crtc1_mode;
    int crtc1_60c0_mystery;
    int crtc1_scan_enable;
    int crtc1_fb_format;
    int crtc1_fb_location;
    int crtc1_fb_end;
    int crtc1_pitch;
    int crtc1_x_length;
    int crtc1_y_length;
    int crtc1_fb_height;
    int crtc1_offset_start;
    int crtc1_offset_end;
    int crtc1_expn_size;
    int crtc1_expn_cntl;
    int crtc1_6594;
    int crtc1_659c;
    int crtc1_65a4;
    int crtc1_65a8;
    int crtc1_65ac;
    int crtc1_65b0;
    int crtc1_65b8;
    int crtc1_65bc;
    int crtc1_65c0;
    int crtc1_65c8;
    int crtc2_h_total;
    int crtc2_h_blank;
    int crtc2_h_sync_wid;
    int crtc2_h_sync_pol;
    int crtc2_v_total;
    int crtc2_v_blank;
    int crtc2_v_sync_wid;
    int crtc2_v_sync_pol;
    int crtc2_cntl;
    int crtc2_mode;
    int crtc2_scan_enable;
    int crtc2_fb_format;
    int crtc2_fb_location;
    int crtc2_fb_end;
    int crtc2_pitch;
    int crtc2_x_length;
    int crtc2_y_length;
    int dac1_cntl;
    int dac1_mystery1;
    int dac1_mystery2;
    int tmds1_cntl;
    int tmds1_mystery1;
    int tmds1_mystery2;
    int tmds1_clock_cntl;
    int tmds1_mystery3;
    int dac2_cntl;
    int dac2_mystery1;
    int dac2_mystery2;
    int tmds2_cntl;
    int tmds2_mystery1;
    int tmds2_mystery2;
    int tmds2_clock_cntl;
    int tmds2_mystery3;
    int cursor1_cntl;
    int cursor1_location;
    int cursor1_size;
    int cursor1_position;
};

struct avivo_info
{
    EntityInfoPtr entity;
    GDevPtr device;
    enum avivo_chip_type chipset;

#ifdef PCIACCESS
    struct pci_device *pci_info;
#else
    pciVideoPtr pci_info;
    PCITAG pci_tag;
#endif
    unsigned char *vbios;
    int rom_header;
    int master_data;
    int is_atom_bios;
    int bpp;
    unsigned long ctrl_addr, fb_addr;
    int ctrl_size, fb_size;
    void *ctrl_base, *fb_base;
    struct avivo_state saved_state;
    Bool (*close_screen)(int, ScreenPtr);
    OptionInfoPtr options;

    DisplayModePtr lfp_fixed_mode;

    unsigned long cursor_offset;
    int cursor_format, cursor_fg, cursor_bg;
    int cursor_width, cursor_height;
    INT16 cursor_x, cursor_y;
};

/*
 * avivo chipset
 */
void avivo_get_chipset(struct avivo_info *avivo);

/*
 * avivo common functions
 */
void avivo_set_indexed(ScrnInfoPtr screen_info,
                       unsigned int index_offset,
                       unsigned int data_offset,
                       unsigned int offset,
                       unsigned int value);
unsigned int avivo_get_indexed(ScrnInfoPtr screen_info,
                               unsigned int index_offset,
                               unsigned int data_offset,
                               unsigned int offset);
unsigned int avivo_get_mc(ScrnInfoPtr screen_info, unsigned int offset);
void avivo_set_mc(ScrnInfoPtr screen_info,
                  unsigned int offset,
                  unsigned int value);
struct avivo_info *avivo_get_info(ScrnInfoPtr screen_info);

/*
 * avivo state handling
 */
void avivo_wait_idle(struct avivo_info *avivo);
void avivo_restore_state(ScrnInfoPtr screen_info);
void avivo_save_state(ScrnInfoPtr screen_info);
void avivo_restore_cursor(ScrnInfoPtr screen_info);
void avivo_save_cursor(ScrnInfoPtr screen_info);

/*
 * avivo crtc handling
 */
Bool avivo_crtc_create(ScrnInfoPtr screen_info);

/*
 * avivo output handling
 */
Bool avivo_output_exist(ScrnInfoPtr screen_info, xf86ConnectorType type,
                        int number, unsigned long ddc_reg);

Bool avivo_output_init(ScrnInfoPtr screen_info, xf86ConnectorType type,
                       int number, unsigned long ddc_reg);

Bool avivo_output_setup(ScrnInfoPtr screen_info);

/*
 * avivo cursor handling
 */
void avivo_cursor_init(ScreenPtr screen);
void avivo_setup_cursor(struct avivo_info *avivo, int id, int enable);

/*
 * avivo memory
 */
void avivo_setup_gpu_memory_map(ScrnInfoPtr screen_info);
FBLinearPtr avivo_xf86AllocateOffscreenLinear(ScreenPtr screen, int length,
        int granularity,
        MoveLinearCallbackProcPtr moveCB,
        RemoveLinearCallbackProcPtr removeCB,
        pointer priv_data);

#endif /* _AVIVO_H_ */
