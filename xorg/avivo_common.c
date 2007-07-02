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
/*
 * avivo common functions
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "avivo.h"
#include "radeon_reg.h"

void
avivo_set_indexed(ScrnInfoPtr screen_info,
                  unsigned int index_offset,
                  unsigned int data_offset,
                  unsigned int offset,
                  unsigned int value)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(index_offset, offset);
    OUTREG(data_offset, value);
}

unsigned int
avivo_get_indexed(ScrnInfoPtr screen_info,
                  unsigned int index_offset,
                  unsigned int data_offset,
                  unsigned int offset)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(index_offset, offset);
    return INREG(data_offset);
}

unsigned int
avivo_get_mc(ScrnInfoPtr screen_info, unsigned int offset)
{
    return avivo_get_indexed(screen_info,
                             AVIVO_MC_INDEX,
                             AVIVO_MC_DATA,
                             offset | 0x007f0000);
}

void
avivo_set_mc(ScrnInfoPtr screen_info,
             unsigned int offset,
             unsigned int value)
{
    avivo_set_indexed(screen_info,
                      AVIVO_MC_INDEX,
                      AVIVO_MC_DATA,
                      offset | 0x00ff0000,
                      value);
}

struct avivo_info *
avivo_get_info(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo;

    if (!screen_info->driverPrivate)
        screen_info->driverPrivate = xcalloc(sizeof(struct avivo_info), 1);

    avivo = screen_info->driverPrivate;
    if (!avivo)
        FatalError("Couldn't allocate driver structure\n");    

    return avivo;
}
