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

#define RADEON_BIOS8(v)  (avivo->vbios[v])
#define RADEON_BIOS16(v) (avivo->vbios[v] | \
                          (avivo->vbios[(v) + 1] << 8))
#define RADEON_BIOS32(v) (avivo->vbios[v] | \
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
    dptr = RADEON_BIOS16(0x18);
    /* If PCI data signature is wrong assume x86 video BIOS anyway */
    if (RADEON_BIOS32(dptr) != (('R' << 24) | ('I' << 16) | ('C' << 8) | 'P')) {
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
        avivo->rom_header = RADEON_BIOS16(0x48);

    if(!avivo->rom_header) {
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "Invalid ROM pointer, BIOS data will not be used\n");
        xfree (avivo->vbios);
        avivo->vbios = NULL;
        return 1;
    }

    tmp = avivo->rom_header + 4;
    if ((RADEON_BIOS8(tmp)   == 'A' &&
         RADEON_BIOS8(tmp+1) == 'T' &&
         RADEON_BIOS8(tmp+2) == 'O' &&
         RADEON_BIOS8(tmp+3) == 'M') ||
        (RADEON_BIOS8(tmp)   == 'M' &&
         RADEON_BIOS8(tmp+1) == 'O' &&
         RADEON_BIOS8(tmp+2) == 'T' &&
         RADEON_BIOS8(tmp+3) == 'A'))
        avivo->is_atom_bios = 1;
    else
        avivo->is_atom_bios = 0;

    if (avivo->is_atom_bios) 
        avivo->master_data = RADEON_BIOS16(avivo->rom_header + 32);

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "%s BIOS detected\n",
               avivo->is_atom_bios ? "ATOM":"Legacy");

    return 0;
}

static struct avivo_connector *
_avivo_find_connector(struct avivo_info *avivo,
                      int connector_num,
                      unsigned int gpio,
                      enum avivo_connector_type type)
{
    struct avivo_connector *connector = avivo->connectors;

    while (connector) {
        if (connector->gpio_base == gpio
            && connector->connector_num == connector_num
            && connector->type == type)
            return connector;
        connector = connector->next;
    }
    return connector;
}

static int
radeon_rom_atom_connectors(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int offset = RADEON_BIOS16(avivo->master_data + 22);
    int tmp, i;

    if (offset == 0) {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "No connector table in BIOS");
        return 1;
    }

    tmp = RADEON_BIOS16(offset + 4);
    for (i = 0; i < 8; i++) {
        if (tmp & (1 << i)) {
            int portinfo, connector_num, connector_type, tmp0;
            unsigned int gpio;
            enum avivo_connector_type type;
            struct avivo_connector *connector;

            portinfo = RADEON_BIOS16(offset + 6 + i * 2);
            connector_num = (portinfo >> 8) & 0xf;
            connector_type = (portinfo >> 4) & 0xf;
            tmp0 = RADEON_BIOS16(avivo->master_data + 24);
            gpio = RADEON_BIOS16(tmp0 + 4 + 27 * connector_num) * 4;
            switch (connector_type) {
            case 0: type = CONNECTOR_UNSUPPORTED; break;
            case 1: type = CONNECTOR_VGA; break;
            case 2: type = CONNECTOR_DVII; break;
            case 3: type = CONNECTOR_DVID; break;
            case 4: type = CONNECTOR_DVIA; break;
            case 5: type = CONNECTOR_STV; break;
            case 6: type = CONNECTOR_CTV; break;
            case 7: type = CONNECTOR_LVDS; break;
            case 8: type = CONNECTOR_DIGITAL; break;
            default: type = CONNECTOR_UNSUPPORTED; break;
            }
            connector = _avivo_find_connector(avivo, connector_num,
                                              gpio, type);
            if (!connector && type != CONNECTOR_UNSUPPORTED) {
                /* This connector haven't been added yet */
                connector = xcalloc(1, sizeof(struct avivo_connector));
                if (!connector)
                    FatalError("Couldn't allocate connector.\n");
                connector->is_connected = 0;
                connector->connector_num = connector_num;
                connector->gpio_base = gpio;
                connector->type = type;
                connector->outputs = NULL;
                connector->next = avivo->connectors;
                avivo->connectors = connector;
            }
        }
    }
    return 0;
}

int
avivo_probe_info(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_crtc *crtc;
    struct avivo_connector *connector;
    struct avivo_output *output;

    if (!avivo)
        FatalError("No driver structure provided for probing\n");
    
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "Set default CRTC list\n");
    if (!avivo->crtcs) {
        /* avivo got 2 crtcs */
        crtc = xcalloc(1, sizeof(struct avivo_crtc));
        if (!crtc)
            FatalError("Couldn't allocate crtcs\n");
        crtc->id = 1;
        crtc->next = avivo->crtcs;
        avivo->crtcs = crtc;

        crtc = xcalloc(1, sizeof(struct avivo_crtc));
        if (!crtc)
            FatalError("Couldn't allocate crtcs\n");
        crtc->id = 0;
        crtc->next = avivo->crtcs;
        avivo->crtcs = crtc;
    }

    if (RADEONGetBIOSInfo(screen_info) ||
        radeon_rom_atom_connectors(screen_info)) {
        /* We allocate default card connector configuration this
         * should be safe for all card.
         */
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "Can't parse bios, set default connector scheme\n");
        connector = xcalloc(1, sizeof(struct avivo_connector));
        if (!connector)
            FatalError("Couldn't allocate connector\n");
        connector->is_connected = 0;
        connector->connector_num = 0;
        connector->gpio_base = AVIVO_GPIO_CONNECTOR_0;
        connector->type = CONNECTOR_DVII;
        connector->outputs = NULL;
        connector->next = avivo->connectors;
        avivo->connectors = connector;

        connector = xcalloc(1, sizeof(struct avivo_connector));
        if (!connector)
            FatalError("Couldn't allocate connector\n");
        connector->is_connected = 0;
        connector->connector_num = 1;
        connector->gpio_base = AVIVO_GPIO_CONNECTOR_1;
        connector->type = CONNECTOR_DVII;
        connector->outputs = NULL;
        connector->next = avivo->connectors;
        avivo->connectors = connector;

#if 0
        /* Should we allocate a default LVDS ? Monitor detection
         * should report not panel thus i believe this won't be
         * harmfull.
         */
        connector = xcalloc(1, sizeof(struct avivo_connector));
        if (!connector)
            FatalError("Couldn't allocate connector\n");
        connector->is_connected = 0;
        connector->connector_num = 2;
        connector->gpio_base = AVIVO_GPIO_LVDS;
        connector->type = CONNECTOR_LVDS;
        connector->outputs = NULL;
        connector->next = avivo->connectors;
        avivo->connectors = connector;
#endif
    }

    /* Now we fill in output for each connector:
     * DVII connector got DAC & TMDS
     * VGA connector got DAC
     * LVDS connector got LVDS
     * It's a fair world, isn't it ?
     */
    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "Connectors:\n");
    connector = avivo->connectors;
    while (connector) {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "\tconnector %d is %d\n",
                   connector->connector_num,
                   connector->type);
        if (connector->type == CONNECTOR_DVII
            || connector->type == CONNECTOR_VGA) {
            output = xcalloc(1, sizeof(struct avivo_output));
            if (!output)
                FatalError("Couldn't allocate output.\n");
            output->crtc = NULL;
            output->is_enabled = 0;
            output->type = OUTPUT_DAC;
            output->status = OUTPUT_OFF;
            output->next = connector->outputs;
            connector->outputs = output;
        }

        if (connector->type == CONNECTOR_DVII) {
            output = xcalloc(1, sizeof(struct avivo_output));
            if (!output)
                FatalError("Couldn't allocate output.\n");
            output->crtc = NULL;
            output->is_enabled = 0;
            output->type = OUTPUT_TMDS;
            output->status = OUTPUT_OFF;
            output->next = connector->outputs;
            connector->outputs = output;
        }

        if (connector->type == CONNECTOR_LVDS) {
            output = xcalloc(1, sizeof(struct avivo_output));
            if (!output)
                FatalError("Couldn't allocate output.\n");
            output->crtc = NULL;
            output->is_enabled = 0;
            output->type = OUTPUT_LVDS;
            output->status = OUTPUT_OFF;
            output->next = connector->outputs;
            connector->outputs = output;
        }

        connector = connector->next;
    }
}


int
avivo_output_clones(ScrnInfoPtr screen_info)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR (screen_info);
    int o, index_mask;

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

    offset = RADEON_BIOS16(avivo->master_data + 22);
    if (offset == 0) {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "No connector table in BIOS");
        return 1;
    }

    tmp = RADEON_BIOS16(offset + 4);
    for (i = 0; i < 8; i++) {
        if (tmp & (1 << i)) {
            int portinfo, number, connector_type, tmp0;
            unsigned int ddc_reg;
            xf86ConnectorType type;    

            portinfo = RADEON_BIOS16(offset + 6 + i * 2);
            number = (portinfo >> 8) & 0xf;
            connector_type = (portinfo >> 4) & 0xf;
            tmp0 = RADEON_BIOS16(avivo->master_data + 24);
            ddc_reg = RADEON_BIOS16(tmp0 + 4 + 27 * number) * 4;
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
            case XF86ConnectorVGA:
            case XF86ConnectorLFP:
            case XF86ConnectorDVI_I:
                avivo_output_init(screen_info, type, number, ddc_reg);
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
