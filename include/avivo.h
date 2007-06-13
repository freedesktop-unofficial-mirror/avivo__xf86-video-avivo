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
#include "xf86_OSproc.h"
#include "xf86Resources.h"
#include "compiler.h"

/* Drivers for PCI hardware need this */
#include "xf86PciInfo.h"

/* Drivers that need to access the PCI config space directly need this */
#include "xf86Pci.h"

#include "xf86Resources.h"
#include "xf86RAC.h"

#include "xf86i2c.h"
#include "xf86DDC.h"

#include "fb.h"

#ifdef PCIACCESS
#include <pciaccess.h>
#endif

#define AVIVO_NAME		"Avivo"
#define AVIVO_DRIVER_NAME	"avivo"
#define AVIVO_DRIVER_VERSION    1000

#define PCI_CHIP_RV515_7142     0x7142
#define PCI_CHIP_RV530_71C2     0x71C2
#define PCI_CHIP_RV530_71C5     0x71C5

#define RADEON_VBIOS_SIZE 0x00010000

#define PCI_CHIP_R580_724B      0x724B

#define INREG(x) MMIO_IN32(avivo->ctrl_base, x)
#define OUTREG(x, y) MMIO_OUT32(avivo->ctrl_base, x, y)

enum avivo_chip_type {
    CHIP_FAMILY_RV515,
    CHIP_FAMILY_R520,
    CHIP_FAMILY_RV530,
    CHIP_FAMILY_R580,
    CHIP_FAMILY_LAST,
};

struct avivo_crtc {
    /* Bitmask of output IDs. */
    int               id;
    int               h_total, h_blank, h_sync_wid, h_sync_pol;
    int               v_total, v_blank, v_sync_wid, v_sync_pol;
    int               clock;
    unsigned long     fb_offset;
    int               fb_format, fb_length;
    int               fb_pitch, fb_width, fb_height;
    struct avivo_crtc *next;
};

enum avivo_output_status {
    OUTPUT_ON,
    OUTPUT_BLANKED,
    OUTPUT_OFF,
};

enum avivo_output_type {
    OUTPUT_DAC,
    OUTPUT_TMDS,
    OUTPUT_LVDS,
    OUTPUT_TV,
};

enum avivo_connector_type {
    CONNECTOR_VGA,
    CONNECTOR_DVII,
    CONNECTOR_DVID,
    CONNECTOR_DVIA,
    CONNECTOR_STV,
    CONNECTOR_CTV,
    CONNECTOR_LVDS,
    CONNECTOR_DIGITAL,
    CONNECTOR_UNSUPPORTED,
};

/**
 * struct avivo_output - avivo output information structure
 * @is_enabled:    is output enabled
 * @gpio_base:     gpio base address register of this connector
 * @type:          output type DAC, TMDS, LVDS, TV
 * @status:        output status
 * @next:          next output
 */
struct avivo_output {
    struct avivo_crtc        *crtc;
    int                      is_enabled;
    enum avivo_output_type   type;
    enum avivo_output_status status;
    struct avivo_output      *next;
};

/**
 * struct avivo_connector - avivo output connector information structure
 * @is_connected:  is output connected
 * @connector_num: connector number
 * @gpio_base:     gpio base address register of this connector
 * @type:          connector type VGA, DVI-I, LVDS, STV, ...
 * @monitor:       monitor information retrieven from DDC
 * @outputs:       associated output
 * @next:          next connector
 */
struct avivo_connector {
    int                       is_connected;
    int                       connector_num;
    unsigned int              gpio_base;
    enum avivo_connector_type type;
    xf86MonPtr                monitor;
    struct avivo_output       *outputs;
    struct avivo_connector    *next;
};

struct avivo_state
{
    int mc_memory_map;
    int vga_memory_base;
    int vga_fb_start;
    int vga_mystery0;
    int vga_mystery1;
    int pll_cntl;
    int pll_post_div;
    int pll_post_mul;
    int pll_divider_cntl;
    int pll_divider;
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
    int crtc1_offset;
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

    I2CBusPtr i2c;
    unsigned int ddc_reg;

    unsigned char *vbios;
    int rom_header;
    int master_data;
    int is_atom_bios;

    
    struct avivo_crtc *crtcs;
    struct avivo_connector *connectors;
    struct avivo_connector *connector_default;

    unsigned long cursor_offset;
    int cursor_format, cursor_fg, cursor_bg;
    int cursor_width, cursor_height;
    INT16 cursor_x, cursor_y;

    unsigned long ctrl_addr, fb_addr;
    int ctrl_size, fb_size;
    void *ctrl_base, *fb_base;

    struct avivo_state saved_state;

    Bool (*close_screen)(int, ScreenPtr);

    OptionInfoPtr options;
};

int avivo_probe_info(ScrnInfoPtr screen_info);
struct avivo_info *avivo_get_info(ScrnInfoPtr screen_info);

/*
 * avivo state handling
 */
void avivo_restore_state(ScrnInfoPtr screen_info);
void avivo_save_state(ScrnInfoPtr screen_info);


/*
 * avivo cursor handling
 */
void avivo_cursor_init(ScreenPtr screen);

#endif /* _AVIVO_H_ */
