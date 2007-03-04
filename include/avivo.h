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

#include "fb.h"

#ifdef PCIACCESS
#include <pciaccess.h>
#endif

#define AVIVO_NAME		"Avivo"
#define AVIVO_DRIVER_NAME	"avivo"
#define AVIVO_DRIVER_VERSION    1000

#define PCI_CHIP_R580_724B      0x724B

#define INREG(x) MMIO_IN32(avivo->ctrl_base, x)
#define OUTREG(x, y) MMIO_OUT32(avivo->ctrl_base, x, y)

enum avivo_chip_type {
    CHIP_FAMILY_RN515,
    CHIP_FAMILY_R520,
    CHIP_FAMILY_R580,
    CHIP_FAMILY_LAST,
};

enum avivo_output_type {
    VGA,
    TMDS,
    LVDS,
    TV,
};

struct avivo_crtc {
    struct avivo_info *avivo;

    /* Bitmask of output IDs. */
    int outputs;

    int id;

    int h_total, h_blank, h_sync_wid, h_sync_pol;
    int v_total, v_blank, v_sync_wid, v_sync_pol;

    unsigned long fb_offset;
    int fb_format, fb_length;
    int fb_pitch, fb_width, fb_height;

    struct avivo_crtc *next;
};

enum avivo_output_status {
    On,
    Blanked,
    Off,
};

struct avivo_output {
    struct avivo_info *avivo;
    struct avivo_crtc *crtc;

    int id;

    /* type TMDS + output_num 2 == TMDS2.
     * type DAC + output_num 1 == DAC1. */
    enum avivo_output_type type;
    int output_num;

    int connected;
    enum avivo_output_status status;

    struct avivo_output *next;
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

    struct avivo_crtc *crtcs;
    struct avivo_output *outputs;

    unsigned long cursor_offset;
    int cursor_format, cursor_fg, cursor_bg;
    int cursor_width, cursor_height;
    INT16 cursor_x, cursor_y;

    unsigned long ctrl_addr, fb_addr;
    int ctrl_size, fb_size;
    void *ctrl_base, *fb_base;

    OptionInfoPtr options;
};

#endif /* _AVIVO_H_ */
