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

#include "xf86str.h"

#include "avivo.h"
#include "radeon_reg.h"
#include <stdint.h>

void
avivo_i2c_gpio0_get_bits(I2CBusPtr b, int *Clock, int *data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    /* Get the result */
    val = INREG(b->DriverPrivate.uval + 0xC);
    *Clock = (val & (1<<19)) != 0;
    *data  = (val & (1<<18)) != 0;
}

void 
avivo_i2c_gpio0_put_bits(I2CBusPtr b, int Clock, int data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    val = 0;
    val |= (Clock ? 0:(1<<19));
    val |= (data ? 0:(1<<18));
    OUTREG(b->DriverPrivate.uval + 0x8, val);
    /* read back to improve reliability on some cards. */
    val = INREG(b->DriverPrivate.uval + 0x8);
}

void
avivo_i2c_gpio123_get_bits(I2CBusPtr b, int *Clock, int *data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    /* Get the result */
    val = INREG(b->DriverPrivate.uval + 0xC);
    *Clock = (val & (1<<0)) != 0;
    *data  = (val & (1<<8)) != 0;
}

void 
avivo_i2c_gpio123_put_bits(I2CBusPtr b, int Clock, int data)
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
