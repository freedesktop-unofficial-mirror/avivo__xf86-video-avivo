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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "avivo.h"
#include "radeon_reg.h"

void
avivo_setup_gpu_memory_map(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long mc_memory_map;
    unsigned long mc_memory_map_end;

    /* init gpu memory mapping */
    mc_memory_map = (avivo->fb_addr >> 16) & AVIVO_MC_MEMORY_MAP_BASE_MASK;
    mc_memory_map_end = ((avivo->fb_addr + avivo->fb_size) >> 16) - 1;
    mc_memory_map |= (mc_memory_map_end << AVIVO_MC_MEMORY_MAP_END_SHIFT)
        & AVIVO_MC_MEMORY_MAP_END_MASK;
    avivo_set_mc(screen_info, AVIVO_MC_MEMORY_MAP, mc_memory_map);
    OUTREG(AVIVO_VGA_MEMORY_BASE,
           (avivo->fb_addr >> 16) & AVIVO_MC_MEMORY_MAP_BASE_MASK);
    OUTREG(AVIVO_VGA_FB_START, avivo->fb_addr);
    avivo_wait_idle(avivo);
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "setup GPU memory mapping\n");
}

FBLinearPtr
avivo_xf86AllocateOffscreenLinear(ScreenPtr screen, int length,
                                  int granularity,
                                  MoveLinearCallbackProcPtr moveCB,
                                  RemoveLinearCallbackProcPtr removeCB,
                                  pointer priv_data)
{
    FBLinearPtr linear;
    int max_size;

    linear = xf86AllocateOffscreenLinear(screen, length, granularity, moveCB,
                                         removeCB, priv_data);
    if (linear != NULL)
        return linear;

    /* The above allocation didn't succeed, so purge unlocked stuff and try
     * again.
     */
    xf86QueryLargestOffscreenLinear(screen, &max_size, granularity,
                                    PRIORITY_EXTREME);
    if (max_size < length)
        return NULL;
    xf86PurgeUnlockedOffscreenAreas(screen);
    return xf86AllocateOffscreenLinear(screen, length, granularity, moveCB,
                                       removeCB, priv_data);
}
