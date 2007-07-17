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

Bool
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

DisplayModePtr
avivo_output_lfp_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr screen_info = output->scrn;
    struct avivo_info *avivo = avivo_get_info(output->scrn);
    DisplayModePtr modes = NULL;
   
    modes = avivo_output_get_modes(output);
    if (modes == NULL) {
        /* DDC EDID failed try to get timing from BIOS */
        xf86DrvMsg(screen_info->scrnIndex, X_WARNING,
                   "Failed to get EDID over i2c for LFP try BIOS timings.\n");
        modes = avivo_bios_get_lfp_timing(screen_info);
    }
    if (modes) {
        xf86DeleteMode(&avivo->lfp_fixed_mode, avivo->lfp_fixed_mode);
        avivo->lfp_fixed_mode = xf86DuplicateMode(modes);
    }
    return modes;
}
