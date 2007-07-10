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
 * avivo output handling functions. 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
/* DPMS */
#define DPMS_SERVER
#include <X11/extensions/dpms.h>

#include "avivo.h"
#include "radeon_reg.h"

static void
avivo_i2c_get_bits(I2CBusPtr b, int *Clock, int *data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    /* Get the result */
    val = INREG(b->DriverPrivate.uval + 0xC);
    *Clock = (val & (1<<0)) != 0;
    *data  = (val & (1<<8)) != 0;
}

static void 
avivo_i2c_put_bits(I2CBusPtr b, int Clock, int data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    val = 0;
    val |= (Clock ? 0:(1<<0));
    val |= (data ? 0:(1<<8));
    OUTREG(b->DriverPrivate.uval + 0x8, val);
    /* read back to improve reliability on some cards. */
    val = INREG(b->DriverPrivate.uval + 0x8);
}

static void
avivo_output_dpms(xf86OutputPtr output, int mode)
{
    ScrnInfoPtr screen_info = output->scrn;
    struct avivo_output_private *avivo_output = output->driver_private;
    struct avivo_info *avivo = avivo_get_info(output->scrn);
    int crtc = 0;

    if (output->crtc) {
        struct avivo_crtc_private *avivo_crtc = output->crtc->driver_private;
        crtc = avivo_crtc->crtc_number;
        xf86DrvMsg(output->scrn->scrnIndex, X_INFO,
                   "%s connector associated to crtc(%d)\n",
                   xf86ConnectorGetName(avivo_output->type), crtc);
    }

    switch (avivo_output->type) {
    case XF86ConnectorVGA:
    {
        int value1, value2, value3;

		value1 = 0;
		value2 = 0;
		value3 = 0;
        switch(mode) {
        case DPMSModeOn:
            value3 = AVIVO_DAC_EN;
            if (!avivo_output->output_offset)
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "enable DAC1\n");
            else
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "enable DAC2\n");
            break;
        case DPMSModeStandby:
        case DPMSModeSuspend:
        case DPMSModeOff:
            value1 = AVIVO_DAC_MYSTERY1_DIS;
            value2 = AVIVO_DAC_MYSTERY2_DIS;
            if (!avivo_output->output_offset)
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "disable DAC1\n");
            else
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "disable DAC2\n");
            break;
        }
        if (output->crtc) {
            OUTREG(AVIVO_DAC1_CRTC_SOURCE + avivo_output->output_offset, crtc);
        }
        OUTREG(AVIVO_DAC1_MYSTERY1 + avivo_output->output_offset, value1);
        OUTREG(AVIVO_DAC1_MYSTERY2 + avivo_output->output_offset, value2);
        OUTREG(AVIVO_DAC1_CNTL + avivo_output->output_offset, value3);
        break;
    }
    case XF86ConnectorLFP:
    case XF86ConnectorDVI_I:
    case XF86ConnectorDVI_D:
    case XF86ConnectorDVI_A:
    {
        int value1, value2, value3, value4, value5;

        value1 = 0;
        value2 = 0;
        value3 = 0x10000011;
        value4 = 0;
        value5 = 0x00001010;
        switch(mode) {
        case DPMSModeOn:
            value1 = AVIVO_TMDS_MYSTERY1_EN;
            value2 = AVIVO_TMDS_MYSTERY2_EN;
            value4 = 0x00001f1f;
            if (avivo_output->number == 2)
                value4 |= 0x00000020;
            value5 |= AVIVO_TMDS_EN;
            if (!avivo_output->output_offset)
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "enable TMDS1\n");
            else
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "enable TMDS2\n");
            break;
        case DPMSModeStandby:
        case DPMSModeSuspend:
        case DPMSModeOff:
            value1 = 0x04000000;
            value2 = 0;
            value4 = 0x00060000;
            if (!avivo_output->output_offset)
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "disable TMDS1\n");
            else
                xf86DrvMsg(screen_info->scrnIndex, X_INFO, "disable TMDS2\n");
            break;
        }
        if (output->crtc) {
            OUTREG(AVIVO_TMDS1_CRTC_SOURCE + avivo_output->output_offset,
                   crtc);
        }
        if (avivo_output->output_offset) {
            value3 |= 0x00630000;
            /* This needs to be set on TMDS, and unset on LVDS. */
            value3 |= INREG(AVIVO_TMDS2_MYSTERY3) & (1 << 29);
            /* This needs to be set on LVDS, and unset on TMDS.  Luckily, the
             * BIOS appears to set it up for us, so just carry it over. */
            value5 |= INREG(AVIVO_TMDS2_CNTL) & (1 << 24);
        }
        OUTREG(AVIVO_TMDS1_MYSTERY1 + avivo_output->output_offset, value1);
        OUTREG(AVIVO_TMDS1_MYSTERY2 + avivo_output->output_offset, value2);
        OUTREG(AVIVO_TMDS1_MYSTERY3 + avivo_output->output_offset, value3);
        OUTREG(AVIVO_TMDS1_CLOCK_CNTL + avivo_output->output_offset, value4);
        OUTREG(AVIVO_TMDS1_CNTL + avivo_output->output_offset, value5);
        break;
    }
    default:
        break;
    }
}

static int
avivo_output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    if (pMode->Flags & V_DBLSCAN)
        return MODE_NO_DBLESCAN;

    if (pMode->Clock > 400000 || pMode->Clock < 25000)
        return MODE_CLOCK_RANGE;

    return MODE_OK;
}

static Bool
avivo_output_mode_fixup(xf86OutputPtr output,
                        DisplayModePtr mode,
                        DisplayModePtr adjusted_mode)
{
    return TRUE;
}

static Bool
avivo_output_lfp_mode_fixup(xf86OutputPtr output,
                            DisplayModePtr mode,
                            DisplayModePtr adjusted_mode)
{
    struct avivo_info *avivo = avivo_get_info(output->scrn);

    if (avivo->lfp_fixed_mode) {
        adjusted_mode->HDisplay = avivo->lfp_fixed_mode->HDisplay;
        adjusted_mode->HSyncStart = avivo->lfp_fixed_mode->HSyncStart;
        adjusted_mode->HSyncEnd = avivo->lfp_fixed_mode->HSyncEnd;
        adjusted_mode->HTotal = avivo->lfp_fixed_mode->HTotal;
        adjusted_mode->VDisplay = avivo->lfp_fixed_mode->VDisplay;
        adjusted_mode->VSyncStart = avivo->lfp_fixed_mode->VSyncStart;
        adjusted_mode->VSyncEnd = avivo->lfp_fixed_mode->VSyncEnd;
        adjusted_mode->VTotal = avivo->lfp_fixed_mode->VTotal;
        adjusted_mode->Clock = avivo->lfp_fixed_mode->Clock;
        xf86SetModeCrtc(adjusted_mode, 0);
    }
    return TRUE;
}

static void
avivo_output_prepare(xf86OutputPtr output)
{
    output->funcs->dpms(output, DPMSModeOff);
}

static void
avivo_output_mode_set(xf86OutputPtr output,
                      DisplayModePtr mode,
                      DisplayModePtr adjusted_mode)
{
}

static void
avivo_output_commit(xf86OutputPtr output)
{
    output->funcs->dpms(output, DPMSModeOn);
}

static Bool
avivo_output_detect_ddc(xf86OutputPtr output)
{
    struct avivo_output_private *avivo_output = output->driver_private;

    return xf86I2CProbeAddress(avivo_output->i2c, 0x00A0);
}

static xf86OutputStatus
avivo_output_detect(xf86OutputPtr output)
{
    if (avivo_output_detect_ddc(output))
        return XF86OutputStatusConnected;
    return XF86OutputStatusUnknown;
}

DisplayModePtr
avivo_output_get_modes(xf86OutputPtr output)
{
    struct avivo_output_private *avivo_output = output->driver_private;
    xf86MonPtr edid_mon;
    DisplayModePtr modes;

    edid_mon = xf86OutputGetEDID(output, avivo_output->i2c);
    xf86OutputSetEDID(output, edid_mon);
    modes = xf86OutputGetEDIDModes(output);
    return modes;
}

DisplayModePtr
avivo_output_lfp_get_modes(xf86OutputPtr output)
{
    struct avivo_info *avivo = avivo_get_info(output->scrn);
    DisplayModePtr modes;
   
    modes = avivo_output_get_modes(output);
    xf86DeleteMode(&avivo->lfp_fixed_mode, avivo->lfp_fixed_mode);
    avivo->lfp_fixed_mode = xf86DuplicateMode(modes);
    return modes;
}

static void
avivo_output_destroy(xf86OutputPtr output)
{
    struct avivo_output_private *avivo_output = output->driver_private;

    if (avivo_output == NULL)
        return;
    xf86DestroyI2CBusRec(avivo_output->i2c, TRUE, TRUE);
    xfree(avivo_output->name);
    xfree(avivo_output);
}

static const xf86OutputFuncsRec avivo_output_funcs = {
    .dpms = avivo_output_dpms,
    .save = NULL,
    .restore = NULL,
    .mode_valid = avivo_output_mode_valid,
    .mode_fixup = avivo_output_mode_fixup,
    .prepare = avivo_output_prepare,
    .mode_set = avivo_output_mode_set,
    .commit = avivo_output_commit,
    .detect = avivo_output_detect,
    .get_modes = avivo_output_get_modes,
    .destroy = avivo_output_destroy
};

static const xf86OutputFuncsRec avivo_output_lfp_funcs = {
    .dpms = avivo_output_dpms,
    .save = NULL,
    .restore = NULL,
    .mode_valid = avivo_output_mode_valid,
    .mode_fixup = avivo_output_lfp_mode_fixup,
    .prepare = avivo_output_prepare,
    .mode_set = avivo_output_mode_set,
    .commit = avivo_output_commit,
    .detect = avivo_output_detect,
    .get_modes = avivo_output_lfp_get_modes,
    .destroy = avivo_output_destroy
};

Bool
avivo_output_exist(ScrnInfoPtr screen_info, xf86ConnectorType type,
                  int number, unsigned long ddc_reg)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(screen_info);
    int i;

    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        struct avivo_output_private *avivo_output = output->driver_private;
        if (avivo_output->number == number && avivo_output->type == type)
            return TRUE;
        /* TMDS2 is shared by LFP & DVI-I */
        if (avivo_output->type == XF86ConnectorLFP && number >= 1)
            return TRUE;
        if (type == XF86ConnectorLFP && avivo_output->number >= 1) {
            avivo_output->type = type;
            avivo_output->i2c->DriverPrivate.uval = ddc_reg;
            return TRUE;
        }
    }
    return FALSE;
}

Bool
avivo_output_init(ScrnInfoPtr screen_info, xf86ConnectorType type,
                  int number, unsigned long ddc_reg)
{
    xf86OutputPtr output;
    struct avivo_output_private *avivo_output;
    int name_size;

    /* allocate & initialize private output structure */
    avivo_output = xcalloc(sizeof(struct avivo_output_private), 1);
    if (avivo_output == NULL)
        return FALSE;
    name_size = snprintf(NULL, 0, "%s connector %d",
                         xf86ConnectorGetName(type), number);
    avivo_output->name = xcalloc(name_size + 1, 1);
    if (avivo_output->name == NULL) {
        xfree(avivo_output);
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Failed to allocate memory for I2C bus name\n");
        return FALSE;
    }
    snprintf(avivo_output->name, name_size + 1, "%s connector %d",
             xf86ConnectorGetName(type), number);
    avivo_output->i2c = xf86CreateI2CBusRec();
    if (!avivo_output->i2c) {
        xfree(avivo_output);
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't create I2C bus for %s connector %d\n",
                   xf86ConnectorGetName(type), number);
        return FALSE;
    }
    avivo_output->i2c->BusName = avivo_output->name;
    avivo_output->i2c->scrnIndex = screen_info->scrnIndex;
    avivo_output->i2c->I2CPutBits = avivo_i2c_put_bits;
    avivo_output->i2c->I2CGetBits = avivo_i2c_get_bits;
    avivo_output->i2c->AcknTimeout = 5;
    avivo_output->i2c->DriverPrivate.uval = ddc_reg;
    if (!xf86I2CBusInit(avivo_output->i2c)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't initialise I2C bus for %s connector %d\n",
                   xf86ConnectorGetName(type), number);
        return FALSE;
    }
    avivo_output->type = type;
    avivo_output->number = number;
    avivo_output->output_offset = 0;
    if (number >= 1) {
        switch (avivo_output->type) {
        case XF86ConnectorVGA:
            avivo_output->output_offset = AVIVO_DAC2_CNTL - AVIVO_DAC1_CNTL;
            break;
        case XF86ConnectorLFP:
        case XF86ConnectorDVI_I:
        case XF86ConnectorDVI_D:
        case XF86ConnectorDVI_A:
            avivo_output->output_offset = AVIVO_TMDS2_CNTL - AVIVO_TMDS1_CNTL;
            break;
        default:
            break;
        }
    }
    if (avivo_output->type == XF86ConnectorLFP) {
        avivo_output->output_offset = AVIVO_TMDS2_CNTL - AVIVO_TMDS1_CNTL;
        /* allocate & initialize xf86Output */
        output = xf86OutputCreate (screen_info,
                                   &avivo_output_lfp_funcs,
                                   xf86ConnectorGetName(type));
    } else {
        /* allocate & initialize xf86Output */
        output = xf86OutputCreate (screen_info,
                                   &avivo_output_funcs,
                                   xf86ConnectorGetName(type));
    }

    if (output == NULL) {
        xf86DestroyI2CBusRec(avivo_output->i2c, TRUE, TRUE);
        xfree(avivo_output);
        return FALSE;
    }
    output->driver_private = avivo_output;
    output->interlaceAllowed = FALSE;
    output->doubleScanAllowed = FALSE;
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "added %s connector %d (0x%04lX)\n",
               xf86ConnectorGetName(type), number, ddc_reg);

	return TRUE;
}
