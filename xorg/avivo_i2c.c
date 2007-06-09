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

void
avivo_wait_idle(struct avivo_info *avivo)
{
    int i = 1000;

    while (--i && INREG(0x6494) != 0x3fffffff);

    if (!i)
        FatalError("Avivo: chip lockup!\n");
}

/**
 * Read num bytes into buf.
 */
static void
avivo_i2c_read(struct avivo_info *avivo, uint8_t *buf, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        *buf = INREG(AVIVO_I2C_DATA) & 0xff;
        buf++;
    }
}

/**
 * Write num bytes from buf.
 */
static void
avivo_i2c_write(struct avivo_info *avivo, uint8_t *buf, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        OUTREG(AVIVO_I2C_DATA, *buf);
        buf++;
    }
}

/**
 * Write the address out on the line, with allowances for extended
 * 11-byte addresses.  According to the server's PutAddress function,
 * we need to send a start, and follow up with a stop if the start
 * succeeds, but the video BIOS doesn't seem to bother, so ...
 */
static void
avivo_i2c_put_address(struct avivo_info *avivo, int addr, int write)
{
    uint8_t buf;

    buf = addr & 0xff;
    if (write)
        buf &= ~1;
    else
        buf |= 1;

    avivo_i2c_write(avivo, &buf, 1);
    if ((addr & 0xf8) == 0xf0 || (addr & 0xfe) == 0) {
        buf = (addr >> 8) & 0xff;
        avivo_i2c_write(avivo, &buf, 1);
    }
}

static void
avivo_i2c_stop(struct avivo_info *avivo)
{
    OUTREG(AVIVO_I2C_STATUS,
           (AVIVO_I2C_STATUS_DONE |
            AVIVO_I2C_STATUS_NACK |
            AVIVO_I2C_STATUS_HALT));
    OUTREG(AVIVO_I2C_STOP, 1);
    OUTREG(AVIVO_I2C_STOP, 0);
}

static int
avivo_i2c_wait_ready(struct avivo_info *avivo)
{
    int i, num_ready, tmp, num_nack;

    OUTREG(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_CMD_WAIT);
    for (i = 0, num_ready = 0, num_nack; num_ready < 3; i++) {
        tmp = INREG(AVIVO_I2C_STATUS);
        if (tmp == AVIVO_I2C_STATUS_DONE) {
            num_ready++;
        }
        else if (tmp == AVIVO_I2C_STATUS_NACK) {
            num_nack++;
        }
        else if (tmp != AVIVO_I2C_STATUS_CMD_WAIT) {
            /* Unknow state observed (so far i have only seen 0x8 or 0x2
             * for i2c status thus we stop i2c if we encounter and unknown
             * status.
             */
            xf86DrvMsg(0, X_ERROR, "I2C bus error\n");
            avivo_i2c_stop(avivo);
            tmp = INREG(AVIVO_I2C_CNTL);
            OUTREG(AVIVO_I2C_CNTL, tmp | AVIVO_I2C_RESET);
            return 1;
        }

        /* Timeout 50ms like on radeon */
        if (i == 50 || num_nack > 3) {
            xf86DrvMsg(0, X_ERROR, "i2c bus timeout\n");
            avivo_i2c_stop(avivo);
            tmp = INREG(AVIVO_I2C_CNTL);
            OUTREG(AVIVO_I2C_CNTL, tmp | AVIVO_I2C_RESET);
            return 1;
        }

        /* If we got more than 3 NACK we stop the bus */
        if (num_nack > 3) {
            avivo_i2c_stop(avivo);
            tmp = INREG(AVIVO_I2C_CNTL);
            OUTREG(AVIVO_I2C_CNTL, tmp | AVIVO_I2C_RESET);
            return 0;
        }

        usleep(1000);
    }
    OUTREG(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_DONE);
    return 0;
}

/**
 * Start the I2C bus, and wait until it's safe to start sending data.
 * For some reason, it looks like we have to read STATUS_READY three
 * times, then write it back.  Obviously.
 */
static void
avivo_i2c_start(struct avivo_info *avivo)
{
    volatile int num_ready, i, tmp;

    tmp = INREG(AVIVO_I2C_CNTL) & AVIVO_I2C_EN;
    if (!(tmp & AVIVO_I2C_EN)) {
        OUTREG(AVIVO_I2C_CNTL, tmp | AVIVO_I2C_EN);
        avivo_i2c_stop(avivo);
        OUTREG(AVIVO_I2C_START_CNTL, AVIVO_I2C_START | AVIVO_I2C_CONNECTOR1);
        tmp = INREG(AVIVO_I2C_7D3C) & (~0xff);
        OUTREG(AVIVO_I2C_7D3C, tmp | 1);
    }
    tmp = INREG(AVIVO_I2C_START_CNTL);
    OUTREG(AVIVO_I2C_START_CNTL, tmp | AVIVO_I2C_START);
}

static Bool
avivo_i2c_write_read(I2CDevPtr i2c, I2CByte *write_buf, int num_write,
                    I2CByte *read_buf, int num_read)
{
    ScrnInfoPtr screen_info = xf86Screens[i2c->pI2CBus->scrnIndex];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    uint8_t i;
    int tmp, size, chunk_size = 12;
    
    for (i = 0; i < num_write; i+= chunk_size) {
        if ((num_write - i) >= chunk_size)
            size = chunk_size;
        else
            size = num_write % chunk_size;
        avivo_i2c_start(avivo);
        tmp = INREG(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
        tmp |= size << AVIVO_I2C_7D3C_SIZE_SHIFT;
        OUTREG(AVIVO_I2C_7D3C, tmp);
        tmp = INREG(AVIVO_I2C_7D40);
        OUTREG(AVIVO_I2C_7D40, tmp);
        avivo_i2c_put_address(avivo, i2c->SlaveAddr, 1);
        avivo_i2c_write(avivo, &write_buf[i], size);
        tmp = INREG(AVIVO_I2C_START_CNTL) & (~AVIVO_I2C_STATUS_MASK);
        OUTREG(AVIVO_I2C_START_CNTL,
               (tmp |
                AVIVO_I2C_STATUS_DONE |
                AVIVO_I2C_STATUS_NACK));
        avivo_i2c_wait_ready(avivo);
    }

    for (i = 0; i < num_read; i+= chunk_size) {
        if ((num_read - i) >= chunk_size)
            size = chunk_size;
        else
            size = num_read % chunk_size;
        avivo_i2c_start(avivo);
        tmp = INREG(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
        tmp |= 1 << AVIVO_I2C_7D3C_SIZE_SHIFT;
        OUTREG(AVIVO_I2C_7D3C, tmp);
        tmp = INREG(AVIVO_I2C_7D40);
        OUTREG(AVIVO_I2C_7D40, tmp);
        avivo_i2c_put_address(avivo, i2c->SlaveAddr, 1);
        avivo_i2c_write(avivo, &i, 1);
        tmp = INREG(AVIVO_I2C_START_CNTL) & (~AVIVO_I2C_STATUS_MASK);
        OUTREG(AVIVO_I2C_START_CNTL,
               (tmp |
                AVIVO_I2C_STATUS_DONE |
                AVIVO_I2C_STATUS_NACK));
        avivo_i2c_wait_ready(avivo);

        avivo_i2c_put_address(avivo, i2c->SlaveAddr, 0);
        tmp = INREG(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
        tmp |= size << AVIVO_I2C_7D3C_SIZE_SHIFT;
        OUTREG(AVIVO_I2C_7D3C, tmp);
        tmp = INREG(AVIVO_I2C_START_CNTL) & (~AVIVO_I2C_STATUS_MASK);
        OUTREG(AVIVO_I2C_START_CNTL,
               (tmp |
                AVIVO_I2C_STATUS_DONE |
                AVIVO_I2C_STATUS_NACK |
                AVIVO_I2C_STATUS_HALT));
        avivo_i2c_wait_ready(avivo);
        avivo_i2c_read(avivo, &read_buf[i], size);
    }

    avivo_i2c_stop(avivo);
}

static xf86MonPtr
avivo_ddc(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    xf86MonPtr monitor;
    int tmp;

    monitor = xf86DoEDID_DDC2(screen_info->scrnIndex, avivo->i2c);
    return monitor;
}

static void
avivo_i2c_init(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    avivo->i2c = xf86CreateI2CBusRec();
    if (!avivo->i2c) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't create I2C bus\n");
        return;
    }

    avivo->i2c->BusName = "DDC";
    avivo->i2c->scrnIndex = screen_info->scrnIndex;
    avivo->i2c->I2CWriteRead = avivo_i2c_write_read;

    if (!xf86I2CBusInit(avivo->i2c)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't initialise I2C bus\n");
        return;
    }
}

static void avivo_i2c_get_bits(I2CBusPtr b, int *Clock, int *data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    /* Get the result */
    val = INREG(avivo->ddc_reg + 0xC);
    *Clock = (val & (1<<0)) != 0;
    *data  = (val & (1<<8)) != 0;
}

static void  avivo_i2c_put_bits(I2CBusPtr b, int Clock, int data)
{
    ScrnInfoPtr screen_info = xf86Screens[b->scrnIndex]; 
    struct avivo_info *avivo = avivo_get_info(screen_info);
    unsigned long  val;

    val = 0;
    val |= (Clock ? 0:(1<<0));
    val |= (data ? 0:(1<<8));
    OUTREG(avivo->ddc_reg + 0x8, val);
    /* read back to improve reliability on some cards. */
    val = INREG(avivo->ddc_reg + 0x8);
}

void
avivo_probe_monitor(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_connector *connector;

    if (!avivo->i2c) {
        avivo->i2c = xf86CreateI2CBusRec();
        if (!avivo->i2c) {
            xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                       "Couldn't create I2C bus\n");
            return;
        }
    }
    avivo->i2c->BusName = "DDC";
    avivo->i2c->scrnIndex = screen_info->scrnIndex;
    avivo->i2c->I2CPutBits = avivo_i2c_put_bits;
    avivo->i2c->I2CGetBits = avivo_i2c_get_bits;
    avivo->i2c->AcknTimeout = 5;
    if (!xf86I2CBusInit(avivo->i2c)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't initialise I2C bus\n");
        return;
    }

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "Going throught i2c...\n");

    avivo->connector_default = avivo->connectors;
    connector = avivo->connectors;
    while (connector) {
        avivo->ddc_reg = connector->gpio_base;
        connector->monitor = xf86DoEDID_DDC2(screen_info->scrnIndex,
                                             avivo->i2c);
        if (connector->monitor) {
            xf86PrintEDID(connector->monitor);
            if (avivo->connector_default->type != CONNECTOR_LVDS) {
                avivo->connector_default = connector;
            }
        }
        connector = connector->next;
    }
}
