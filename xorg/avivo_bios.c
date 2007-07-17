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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "avivo.h"
#include "radeon_reg.h"

#define ATOM_OFFSET_ROM_HEADER_OFFSET                       72
#define     ATOM_ROM_HEADER_MASTER_OFFSET                       32
#define     ATOM_MASTER_LFP_OFFSET                              16
#define     ATOM_LFP_XRES                                       6
#define     ATOM_LFP_YRES                                       10
#define     ATOM_LFP_DOT_CLOCK                                  4
#define     ATOM_LFP_HBLANK                                     8
#define     ATOM_LFP_HOVER_PLUS                                 14
#define     ATOM_LFP_HSYNC_WIDTH                                16
#define     ATOM_LFP_VBLANK                                     12
#define     ATOM_LFP_VOVER_PLUS                                 18
#define     ATOM_LFP_VSYNC_WIDTH                                20
#define     ATOM_LFP_POWER_ON_DELAY                             40

#define BIOS8(v)  (avivo->vbios[v])
#define BIOS16(v) (avivo->vbios[v] | \
                          (avivo->vbios[(v) + 1] << 8))
#define BIOS32(v) (avivo->vbios[v] | \
                          (avivo->vbios[(v) + 1] << 8) | \
                          (avivo->vbios[(v) + 2] << 16) | \
                          (avivo->vbios[(v) + 3] << 24))

/* Read the Video BIOS block and the FP registers (if applicable). */
static Bool
RADEONGetBIOSInfo(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int tmp;
    unsigned short dptr;

#ifdef PCIACCESS
    if (!(avivo->vbios = xalloc(avivo->pci_avivo->rom_size))) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Cannot allocate space for hold Video BIOS!\n");
        return 1;
    }
    if (pci_device_read_rom(avivo->pci_info, avivo->vbios)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Failed to read video BIOS!\n");
        xfree (avivo->vbios);
        avivo->vbios = NULL;
        return 1;
    }
#else
    if (!(avivo->vbios = xalloc(RADEON_VBIOS_SIZE))) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Cannot allocate space for hold Video BIOS!\n");
        return 1;
    }
    xf86ReadPciBIOS(0, avivo->pci_tag, 0, avivo->vbios, RADEON_VBIOS_SIZE);
#endif

    if (avivo->vbios[0] != 0x55 || avivo->vbios[1] != 0xaa) {
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "Unrecognized BIOS signature, BIOS data will not be used\n");
        xfree (avivo->vbios);
        avivo->vbios = NULL;
        return 1;
    }

    /* Verify it's an x86 BIOS not OF firmware, copied from radeonfb */
    dptr = BIOS16(0x18);
    /* If PCI data signature is wrong assume x86 video BIOS anyway */
    if (BIOS32(dptr) != (('R' << 24) | ('I' << 16) | ('C' << 8) | 'P')) {
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "ROM PCI data signature incorrect, ignoring\n");
    }
    else if (avivo->vbios[dptr + 0x14] != 0x0) {
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "Not an x86 BIOS ROM image, BIOS data will not be used\n");
        xfree (avivo->vbios);
        avivo->vbios = NULL;
        return 1;
    }

    if (avivo->vbios)
        avivo->rom_header = BIOS16(0x48);

    if(!avivo->rom_header) {
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "Invalid ROM pointer, BIOS data will not be used\n");
        xfree (avivo->vbios);
        avivo->vbios = NULL;
        return 1;
    }

    tmp = avivo->rom_header + 4;
    if ((BIOS8(tmp)   == 'A' &&
         BIOS8(tmp+1) == 'T' &&
         BIOS8(tmp+2) == 'O' &&
         BIOS8(tmp+3) == 'M') ||
        (BIOS8(tmp)   == 'M' &&
         BIOS8(tmp+1) == 'O' &&
         BIOS8(tmp+2) == 'T' &&
         BIOS8(tmp+3) == 'A'))
        avivo->is_atom_bios = 1;
    else
        avivo->is_atom_bios = 0;

    if (avivo->is_atom_bios) 
        avivo->master_offset = BIOS16(avivo->rom_header + 32);

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "%s BIOS detected\n",
               avivo->is_atom_bios ? "ATOM":"Legacy");

    return 0;
}

int
avivo_output_clones(ScrnInfoPtr screen_info)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(screen_info);
    int o, index_mask = 0;

    for (o = 0; o < config->num_output; o++) {
	    index_mask |= (1 << o);
    }
    return index_mask;
}

Bool
avivo_output_setup(ScrnInfoPtr screen_info)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(screen_info);
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int offset = 0;
    int tmp, i, j;

    if (RADEONGetBIOSInfo(screen_info))
        return FALSE;

    offset = BIOS16(avivo->master_offset + 22);
    if (offset == 0) {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "No connector table in BIOS");
        return 1;
    }

    tmp = BIOS16(offset + 4);
    for (i = 0; i < 8; i++) {
        if (tmp & (1 << i)) {
            int portinfo, number, connector_type, tmp0;
            unsigned int ddc_reg;
            xf86ConnectorType type;    

            portinfo = BIOS16(offset + 6 + i * 2);
            number = (portinfo >> 8) & 0xf;
            connector_type = (portinfo >> 4) & 0xf;
            tmp0 = BIOS16(avivo->master_offset + 24);
            ddc_reg = BIOS16(tmp0 + 4 + 27 * number) * 4;
            switch (connector_type) {
            case 0: type = XF86ConnectorNone; break;
            case 1: type = XF86ConnectorVGA; break;
            case 2: type = XF86ConnectorDVI_I; break;
            case 3: type = XF86ConnectorDVI_D; break;
            case 4: type = XF86ConnectorDVI_A; break;
            case 5: type = XF86ConnectorSvideo; break;
            case 6: type = XF86ConnectorComponent; break;
            case 7: type = XF86ConnectorLFP; break;
            case 8: type = XF86ConnectorNone; break;
            default: type = XF86ConnectorNone; break;
            }

            switch (type) {
            case XF86ConnectorLFP:
	    	number = 1;
            case XF86ConnectorVGA:
            case XF86ConnectorDVI_I:
	    	if (!avivo_output_exist(screen_info, type, number, ddc_reg))
                    avivo_output_init(screen_info, type, number, ddc_reg);
                break;
            default:
                break;
            }
        }
    }
    /* check that each DVI-I output also has a VGA output */
    for (i = 0; i < config->num_output; i++) {
        int vga = 0;
        xf86OutputPtr output = config->output[i];
        struct avivo_output_private *avivo_output = output->driver_private;
        if (avivo_output->type == XF86ConnectorDVI_I) {
            for (j = 0; j < config->num_output; j++) {
                xf86OutputPtr o = config->output[j];
                struct avivo_output_private *ao = o->driver_private;
                if (ao->type == XF86ConnectorVGA
                    && ao->number == avivo_output->number
                    && ao->i2c->DriverPrivate.uval
                    == avivo_output->i2c->DriverPrivate.uval) {
                    vga = 1;
                    break;
                }
            }
            if (!vga) {
                avivo_output_init(screen_info, XF86ConnectorVGA,
                                  avivo_output->number,
                                  avivo_output->i2c->DriverPrivate.uval);
            }
        }
    }

    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        output->possible_crtcs = (1 << 0) | (1 << 1);
        output->possible_clones = avivo_output_clones(screen_info);
    }

    /* Set LFP possible crtc so that only crtc1 is used this isn't
     * necessary but this make easier to compare with fglrx for
     * time being as fglrx always use crtc1 with LFP where avivo
     * will more likely use crtc2 due to the order of connector
     * in bios table.
     */
    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        struct avivo_output_private *avivo_output = output->driver_private;
        if (avivo_output->type == XF86ConnectorLFP) {
            output->possible_crtcs = (1 << 0);
        }
    }

    return TRUE;
}

DisplayModePtr
avivo_bios_get_lfp_timing(ScrnInfoPtr screen_info)
{
    DisplayModePtr mode;
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int offset;

    if (!avivo->is_atom_bios || avivo->vbios == NULL)
        return NULL;
    offset = BIOS16(avivo->master_offset + ATOM_MASTER_LFP_OFFSET);
    if (!offset)
        return NULL;
    mode             = xnfcalloc(1, sizeof(DisplayModeRec)); 
    mode->name       = xnfalloc(32);
    snprintf(mode->name, 32, "%dx%d",
             BIOS16(offset + ATOM_LFP_XRES),
             BIOS16(offset + ATOM_LFP_YRES));
    mode->HDisplay   = BIOS16(offset + ATOM_LFP_XRES);
    mode->VDisplay   = BIOS16(offset + ATOM_LFP_YRES);
    mode->HTotal     = mode->HDisplay + BIOS16(offset + ATOM_LFP_HBLANK);
    mode->HSyncStart = mode->HDisplay + BIOS16(offset + ATOM_LFP_HOVER_PLUS);
    mode->HSyncEnd   = mode->HSyncStart + BIOS16(offset + ATOM_LFP_HSYNC_WIDTH);
    mode->VTotal     = mode->VDisplay + BIOS16(offset + ATOM_LFP_VBLANK);
    mode->VSyncStart = mode->VDisplay + BIOS16(offset + ATOM_LFP_VOVER_PLUS);
    mode->VSyncEnd   = mode->VSyncStart + BIOS16(offset + ATOM_LFP_VSYNC_WIDTH);
    mode->Clock      = BIOS16(offset + ATOM_LFP_DOT_CLOCK) * 10;
    mode->Flags      = 0;
    mode->type       = M_T_USERDEF | M_T_PREFERRED;
    mode->next       = NULL;
    mode->prev       = NULL;
    return mode;
}
