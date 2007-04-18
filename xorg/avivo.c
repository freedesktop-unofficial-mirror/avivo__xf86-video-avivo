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
 * This is quite a primitive driver.  It's like radeontool, but in driver
 * form.  It doesn't support offscreen allocation.  Largely because it
 * _doesn't have an allocator_.  Not much point since there's no
 * acceleration yet, anyway.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "micmap.h"
#include "cursorstr.h"
#include "xf86Cursor.h"
#include "xf86str.h"

#ifdef WITH_VGAHW
#include "vgaHW.h"
#endif

#include "avivo.h"
#include "radeon_reg.h"

/* DPMS */
#define DPMS_SERVER
#include <X11/extensions/dpms.h>

/* Mandatory functions */
static const OptionInfoRec *avivo_available_options(int chipid, int busid);
static void avivo_identify(int flags);
static Bool avivo_old_probe(DriverPtr driver, int flags);
#ifdef PCIACCESS
static Bool avivo_pci_probe(DriverPtr driver, int entity_num,
                            struct pci_device *dev, intptr_t match_data);
#endif
static Bool avivo_preinit(ScrnInfoPtr screen_info, int flags);
static Bool avivo_screen_init(int index, ScreenPtr screen, int argc,
                              char **argv);
static Bool avivo_enter_vt(int index, int flags);
static void avivo_leave_vt(int index, int flags);
static Bool avivo_close_screen(int index, ScreenPtr screen);
static Bool avivo_save_screen(ScreenPtr screen, int mode);

static Bool avivo_switch_mode(int index, DisplayModePtr mode, int flags);
static Bool avivo_set_mode(ScrnInfoPtr screen_info, DisplayModePtr mode);
static void avivo_adjust_frame(int index, int x, int y, int flags);
static void avivo_free_screen(int index, int flags);
static void avivo_free_info(ScrnInfoPtr screen_info);

static struct avivo_info *avivo_get_info(ScrnInfoPtr screen_info);

static void avivo_dpms(ScrnInfoPtr screen_info, int mode, int flags);

#ifdef PCIACCESS
static const struct pci_id_match avivo_device_match[] = {
    {
        PCI_VENDOR_ATI, 0x71c5, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0
    },
    {
        PCI_VENDOR_ATI, 0x724b, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0
    },
    {
        PCI_VENDOR_ATI, 0x7142, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0      
    },

    { 0, 0, 0 },
};
#endif
    
/* Supported chipsets.  I'm really, really glad that these are
 * separate, and the nomenclature is beyond reproach. */
static SymTabRec avivo_chips[] = {
    { PCI_CHIP_RV515_7142, "RV515 (Radeon X1300)" },
    { PCI_CHIP_RV530_71C5, "RV530 (Radeon X1600)" },
    { PCI_CHIP_R580_724B,  "R580 (Radeon X1900 GT)" },
    { -1,                  NULL }
};

static PciChipsets avivo_pci_chips[] = {
  { PCI_CHIP_RV530_71C5, PCI_CHIP_RV530_71C5, RES_SHARED_VGA },
  { PCI_CHIP_R580_724B,  PCI_CHIP_R580_724B,  RES_SHARED_VGA },
  { PCI_CHIP_RV515_7142, PCI_CHIP_RV515_7142, RES_SHARED_VGA },
  { -1,                  -1,                  RES_UNDEFINED }
};

/* 
 * This contains the functions needed by the server after loading the
 * driver module.  It must be supplied, and gets added the driver list by
 * the Module Setup funtion in the dynamic case.  In the static case a
 * reference to this is compiled in, and this requires that the name of
 * this DriverRec be an upper-case version of the driver name.
 */
_X_EXPORT DriverRec avivo_driver = {
    AVIVO_DRIVER_VERSION,
    "avivo",
    avivo_identify,
    avivo_old_probe,
    avivo_available_options,
    NULL,
    0,
    NULL,

#ifdef PCIACCESS
    avivo_device_match,
    avivo_pci_probe,
#endif
};


enum avivo_option_type {
    OPTION_LAYOUT,
};

static const OptionInfoRec avivo_options[] = {
    { OPTION_LAYOUT,       "MonitorLayout",     OPTV_STRING,    { 0 },  FALSE },
    { -1,                  NULL,                OPTV_NONE,      { 0 },  FALSE }
};

/* Module loader interface */
static MODULESETUPPROTO(avivo_setup);

static XF86ModuleVersionInfo avivo_version = {
    "avivo",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    { 0, 0, 0, 0, },
};

/*
 * This data is accessed by the loader.  The name must be the module name
 * followed by "ModuleData".
 */
_X_EXPORT XF86ModuleData avivoModuleData = { &avivo_version, avivo_setup, NULL };

static int
avivo_info_probe(struct avivo_info *avivo)
{
    struct avivo_crtc *crtc;
    struct avivo_output *output;

    if (!avivo)
        FatalError("No driver structure provided for probing\n");

    if (!avivo->crtcs) {
        avivo->crtcs = xcalloc(sizeof(struct avivo_crtc), 2);
        if (!avivo->crtcs)
            FatalError("Couldn't allocate outputs\n");

        crtc = avivo->crtcs;
        crtc->avivo = avivo;
        crtc->id = 1;
        crtc->next = crtc + 1;

        crtc = crtc->next;
        crtc->avivo = avivo;
        crtc->id = 2;
        crtc->next = NULL;
    }

    if (!avivo->outputs) {
        avivo->outputs = xcalloc(sizeof(struct avivo_output), 4);
        if (!avivo->outputs)
            FatalError("Couldn't allocate outputs\n");

        output = avivo->outputs;
        output->avivo = avivo;
        output->type = TMDS;
        output->output_num = 1;
        output->crtc = avivo->crtcs;
        output->id = 1;
        output->status = Off;
        output->next = output + 1;

        output = output->next;
        output->type = TMDS;
        output->avivo = avivo;
        output->output_num = 2;
        output->crtc = avivo->crtcs;
        output->id = 2;
        output->status = Off;
        output->next = output + 1;

        output = output->next;
        output->type = VGA;
        output->avivo = avivo;
        output->output_num = 1;
        output->crtc = avivo->crtcs;
        output->id = 3;
        output->status = Off;
        output->next = output + 1;

        output = output->next;
        output->type = VGA;
        output->avivo = avivo;
        output->output_num = 2;
        output->crtc = avivo->crtcs;
        output->id = 4;
        output->status = Off;
        output->next = NULL;
    }
}

static struct avivo_info *
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

static int
avivo_map_ctrl_mem(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int i;

    if (avivo->ctrl_base)
        return 1;

#ifdef PCIACCESS
    return 0;
#else
    avivo->ctrl_base = xf86MapPciMem(screen_info->scrnIndex,
                                     VIDMEM_MMIO | VIDMEM_READSIDEEFFECT,
                                     avivo->pci_tag, avivo->ctrl_addr,
                                     avivo->ctrl_size);
#endif
    if (!avivo->ctrl_base) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't map control memory at %p", avivo->ctrl_addr);
        return 0;
    }
}

static int
avivo_map_fb_mem(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    int i = 0;

    if (avivo->fb_base)
        return 0;

#ifdef PCIACCESS
    return 0;
#else
    avivo->fb_base = xf86MapPciMem(screen_info->scrnIndex, VIDMEM_FRAMEBUFFER,
                                   avivo->pci_tag, avivo->fb_addr,
                                   avivo->fb_size);
#endif
    if (!avivo->fb_base) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't map fb memory at %p", avivo->fb_addr);
        return 0;
    }
    screen_info->memPhysBase = 0;
    screen_info->fbOffset = 0;

    return 1;
}

static void
avivo_unmap_ctrl_mem(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

#ifdef PCIACCESS
    ;
#else
    xf86UnMapVidMem(screen_info->scrnIndex, avivo->ctrl_base, avivo->ctrl_size);
#endif
    avivo->ctrl_base = NULL;
}

static void
avivo_unmap_fb_mem(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

#ifdef PCIACCESS
    ;
#else
    xf86UnMapVidMem(screen_info->scrnIndex, avivo->fb_base, avivo->fb_size);
#endif

    avivo->fb_base = NULL;
}

static pointer
avivo_setup(pointer module, pointer options, int *err_major, int *err_minor)
{
    static Bool inited = FALSE;

    if (!inited) {
        inited = TRUE;
        xf86AddDriver(&avivo_driver, module, 1);
        return (pointer) TRUE;
    }

    if (err_major)
        *err_major = LDR_ONCEONLY;

    return NULL;
}

static const OptionInfoRec *
avivo_available_options(int chipid, int busid)
{
    return avivo_options;
}

static void
avivo_identify(int flags)
{
    xf86PrintChipsets("Avivo", "driver for Radeon r5xx chipsets",
                      avivo_chips);
}

void fill_in_screen(ScrnInfoPtr screen_info)
{
    screen_info->driverVersion = AVIVO_DRIVER_VERSION;
    screen_info->driverName = "avivo";
    screen_info->name = "Avivo";
    screen_info->Probe = avivo_old_probe;
    screen_info->PreInit = avivo_preinit;
    screen_info->ScreenInit = avivo_screen_init;
    screen_info->SwitchMode = avivo_switch_mode;
    screen_info->AdjustFrame = avivo_adjust_frame;
    screen_info->EnterVT = avivo_enter_vt;
    screen_info->LeaveVT = avivo_leave_vt;
    screen_info->FreeScreen = avivo_free_screen;
}

/*
 * This function is called once, at the start of the first server generation to
 * do a minimal probe for supported hardware.
 */

#ifdef PCIACCESS
static Bool
avivo_pci_probe(DriverPtr drv, int entity_num, struct pci_device *dev,
                intptr_t match_data)
{
    ScrnInfoPtr screen_info;
    struct avivo_info *avivo;
    
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NULL, 
                                NULL, NULL, NULL, NULL, NULL);
    if (pScrn) {
        avivo = avivo_get_info(screen_info);
        fill_in_screen(screen_info);
        avivo->pci_info = dev;
    }
    
    return !!screen_info;
}
#endif

/* God, this is a crap interface.  No wonder we don't use it any more. */
static Bool
avivo_old_probe(DriverPtr drv, int flags)
{
    struct avivo_info avivo;
    ScrnInfoPtr screen_info = NULL;
    GDevPtr *sections;
    Bool found_screen = FALSE;
    int num_sections, used_sections;
    int *used_chips;
    int i;

    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    num_sections = xf86MatchDevice("avivo", &sections);
    if (num_sections <= 0)
        return FALSE;

#ifndef PCIACCESS
    used_sections = xf86MatchPciInstances(AVIVO_NAME, PCI_VENDOR_ATI,
                                          avivo_chips, avivo_pci_chips, 
                                          sections, num_sections, drv,
                                          &used_chips);

    if (used_sections > 0) {
        if (flags & PROBE_DETECT) {
            found_screen = TRUE;
        }
        else {
            for (i = 0; i < used_sections; i++) {
                screen_info = xf86ConfigPciEntity(screen_info, 0, used_chips[i],
                                                  avivo_pci_chips, NULL,
                                                  NULL, NULL, NULL, NULL);
                if (screen_info) {
                    fill_in_screen(screen_info);
                    found_screen = TRUE;
                }
            }
        }

        xfree(used_chips);
    }
#endif

    xfree(sections);

    return found_screen;
}

static void
avivo_free_info(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    xfree(screen_info->driverPrivate);
    screen_info->driverPrivate = NULL;
}

static void
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
    int i, num_ready, tmp;

    OUTREG(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_CMD_WAIT);
    for (i = 0, num_ready = 0; num_ready < 3; i++) {
        tmp = INREG(AVIVO_I2C_STATUS);
        if (tmp == AVIVO_I2C_STATUS_DONE) {
            num_ready++;
        }
        else if (tmp != AVIVO_I2C_STATUS_CMD_WAIT) {
            /* FIXME: We probably need to abort in this case; is i2c_stop
             *        enough, or do we need NACK/ABORT? */
            xf86DrvMsg(0, X_ERROR, "I2C bus error\n");
            avivo_i2c_stop(avivo);
            OUTREG(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_CMD_WAIT);
            return 0;
        }

        /* Timeout 50ms like on radeon. */
        if (i == 50) {
            xf86DrvMsg(0, X_ERROR, "timeout waiting for engine to go ready\n");
            tmp = INREG(AVIVO_I2C_CNTL) & ~(AVIVO_I2C_EN);
            OUTREG(AVIVO_I2C_CNTL, tmp);
            return 0;
        }

        usleep(1000);
    }
    OUTREG(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_DONE);
    return 1;
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
    if (!tmp) {
        OUTREG(AVIVO_I2C_CNTL, AVIVO_I2C_EN);
        OUTREG(AVIVO_I2C_START_CNTL, AVIVO_I2C_START | AVIVO_I2C_CONNECTOR2);
        tmp = INREG(AVIVO_I2C_7D3C) & (~0xff);
        OUTREG(AVIVO_I2C_7D3C, tmp | 1);
        avivo_i2c_stop(avivo);
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
        OUTREG(AVIVO_I2C_START_CNTL,
               (AVIVO_I2C_START |
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
        OUTREG(AVIVO_I2C_START_CNTL,
               (AVIVO_I2C_START |
                AVIVO_I2C_STATUS_DONE |
                AVIVO_I2C_STATUS_NACK));
        avivo_i2c_wait_ready(avivo);

        avivo_i2c_put_address(avivo, i2c->SlaveAddr, 0);
        tmp = INREG(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
        tmp |= size << AVIVO_I2C_7D3C_SIZE_SHIFT;
        OUTREG(AVIVO_I2C_7D3C, tmp);
        OUTREG(AVIVO_I2C_START_CNTL,
               (AVIVO_I2C_START |
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

static void
avivo_cursor_show(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(AVIVO_CURSOR1_CNTL, INREG(AVIVO_CURSOR1_CNTL) | AVIVO_CURSOR_EN);
}

static void
avivo_cursor_hide(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    OUTREG(AVIVO_CURSOR1_CNTL, INREG(AVIVO_CURSOR1_CNTL) & ~(AVIVO_CURSOR_EN));
}

static void
avivo_cursor_set_position(ScrnInfoPtr screen_info, int x, int y)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    OUTREG(AVIVO_CURSOR1_POSITION, (x << 16) | y);

    avivo->cursor_x = x;
    avivo->cursor_y = y;
}

static void
avivo_setup_cursor(struct avivo_info *avivo, int id, int enable)
{
    if (id == 1) {
        OUTREG(AVIVO_CURSOR1_CNTL, 0);

        if (enable) {
            OUTREG(AVIVO_CURSOR1_LOCATION, avivo->fb_addr +
                                           avivo->cursor_offset);
            OUTREG(AVIVO_CURSOR1_SIZE, (avivo->cursor_width << 16) |
                                       avivo->cursor_height);
            OUTREG(AVIVO_CURSOR1_CNTL, AVIVO_CURSOR_EN |
                                       (avivo->cursor_format <<
                                        AVIVO_CURSOR_FORMAT_SHIFT));
        }
    }
}


static void
avivo_cursor_load_argb(ScrnInfoPtr screen_info, CursorPtr cursor)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    CARD32 *dst = (CARD32 *)(avivo->fb_base + avivo->cursor_offset);
    CARD32 *src;
    int x, y;

    for (y = 0; y < cursor->bits->height; y++) {
        src = cursor->bits->argb + (y * cursor->bits->width);
        for (x = 0; x < cursor->bits->width; x++)
            *dst++ = *src++;
        for (x = cursor->bits->width; x < 64; x++)
            *dst++ = 0;
    }
    for (y = cursor->bits->height; y < 64; y++) {
        for (x = 0; x < 64; x++)
            *dst++ = 0;
    }

    avivo->cursor_width = cursor->bits->width;
    avivo->cursor_height = cursor->bits->height;
    avivo->cursor_format = AVIVO_CURSOR_FORMAT_ARGB;

    avivo_setup_cursor(avivo, 1, 1);
}

/* Mono ARGB cursor colours (premultiplied). */
static CARD32 mono_cursor_color[] = {
        0x00000000, /* White, fully transparent. */
        0x00000000, /* Black, fully transparent. */
        0xffffffff, /* White, fully opaque. */
        0xff000000, /* Black, fully opaque. */
};

static void
avivo_cursor_load_image(ScrnInfoPtr screen_info, unsigned char *bits)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    CARD32 *d = (CARD32 *)(avivo->fb_base + avivo->cursor_offset);
    CARD8 *s;
    CARD8 chunk;
    int i, j;

#define ARGB_PER_CHUNK  (8 * sizeof (chunk) / 2)
    s = bits;
    for (i = 0; i < (64 * 64 / ARGB_PER_CHUNK); i++) {
        chunk = *s++;
        for (j = 0; j < ARGB_PER_CHUNK; j++, chunk >>= 2)
            *d++ = mono_cursor_color[chunk & 3];
    }

    avivo->cursor_bg = mono_cursor_color[2];
    avivo->cursor_fg = mono_cursor_color[3];
    avivo->cursor_width = 63;
    avivo->cursor_height = 63;
    avivo->cursor_format = AVIVO_CURSOR_FORMAT_ARGB;

    avivo_setup_cursor(avivo, 1, 1);
}

static void
avivo_cursor_set_colors(ScrnInfoPtr screen_info, int bg, int fg)
{
    /* FIXME implement */ ;
}

static void
avivo_cursor_init(ScreenPtr screen)
{
    ScrnInfoPtr screen_info = xf86Screens[screen->myNum];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    xf86CursorInfoPtr cursor;

    cursor = xcalloc(1, sizeof(*cursor));
    if (!cursor)
        FatalError("Couldn't create cursor info\n");

    cursor->MaxWidth = 64;
    cursor->MaxHeight = 64;
    cursor->Flags = (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
                     HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
                     HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1);
    cursor->SetCursorPosition = avivo_cursor_set_position;
    cursor->LoadCursorARGB = avivo_cursor_load_argb;
    cursor->LoadCursorImage = avivo_cursor_load_image;
    cursor->SetCursorColors = avivo_cursor_set_colors;
    cursor->HideCursor = avivo_cursor_hide;
    cursor->ShowCursor = avivo_cursor_show;

    if (!xf86InitCursor(screen, cursor))
        FatalError("Couldn't initialise cursor\n");
}

static void
avivo_get_chipset(struct avivo_info *avivo)
{
    switch (avivo->pci_info->chipType) {
    case PCI_CHIP_RV530_71C5:
        avivo->chipset = CHIP_FAMILY_RV530;
        break;

    case PCI_CHIP_R580_724B:
        avivo->chipset = CHIP_FAMILY_R580;    
        break;

    case PCI_CHIP_RV515_7142:
        avivo->chipset = CHIP_FAMILY_RV515;    
        break;

    default:
        FatalError("Unknown chipset for %x!\n", avivo->pci_info->device);
        break;
    }
}

/*
 * This function is called once for each screen at the start of the first
 * server generation to initialise the screen for all server generations.
 */
static Bool
avivo_preinit(ScrnInfoPtr screen_info, int flags)
{
    struct avivo_info *avivo;
    DisplayModePtr mode;
    ClockRangePtr clock_ranges;
    xf86MonPtr monitor;
    char *mod = NULL;
    int i;
    Gamma gzeros = { 0.0, 0.0, 0.0 };
    rgb rzeros = { 0, 0, 0 };

    if (flags & PROBE_DETECT)
        return FALSE;

    if (!xf86LoadSubModule(screen_info, "fb"))
        FatalError("Couldn't load fb\n");
    if (!xf86LoadSubModule(screen_info, "ramdac"))
        FatalError("Couldn't load ramdac\n");

    avivo = avivo_get_info(screen_info);
    avivo->entity = xf86GetEntityInfo(screen_info->entityList[0]);
    avivo->device = xf86GetDevFromEntity(screen_info->entityList[0],
                                         screen_info->entityInstanceList[0]);

#ifndef PCIACCESS
    avivo->pci_info = xf86GetPciInfoForEntity(avivo->entity->index);
    avivo->pci_tag = pciTag(avivo->pci_info->bus, avivo->pci_info->device,
                            avivo->pci_info->func);

    /* Map MMIO space first, then the framebuffer. */
    for (i = 0; i < 6; i++) {
        if (avivo->pci_info->size[i] == 15 || avivo->pci_info->size[i] == 16) {
            avivo->ctrl_addr = avivo->pci_info->memBase[i] & 0xffffff00;
            avivo->ctrl_size = (1 << avivo->pci_info->size[i]);
            avivo_map_ctrl_mem(screen_info);
        }
    }

    for (i = 0; i < 6; i++) {
        if (avivo->pci_info->size[i] >= 26) {
            avivo->fb_addr = avivo->pci_info->memBase[i] & 0xfe000000;
            avivo->fb_size = INREG(RADEON_CONFIG_MEMSIZE);
            screen_info->videoRam = avivo->fb_size / 1024;
            avivo_map_fb_mem(screen_info);
        }
    }
#endif

    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "Control memory at %p, fb at %p\n", avivo->ctrl_addr,
               avivo->fb_addr);

    avivo_get_chipset(avivo);

    screen_info->chipset = "avivo";
    screen_info->monitor = screen_info->confScreen->monitor;

    /* probe BIOS information */
    avivo_info_probe(avivo);

    if (!xf86SetDepthBpp(screen_info, 0, 0, 0, Support32bppFb))
        return FALSE;
    xf86PrintDepthBpp(screen_info);

    /* color weight */
    if (!xf86SetWeight(screen_info, rzeros, rzeros))
        return FALSE;

    /* visual init */
    if (!xf86SetDefaultVisual(screen_info, -1))
        return FALSE;

    xf86SetGamma(screen_info, gzeros);

    avivo_i2c_init(screen_info);

    screen_info->monitor = screen_info->confScreen->monitor;
    monitor = avivo_ddc(screen_info);
    if (monitor)
        xf86SetDDCproperties(screen_info, xf86PrintEDID(monitor));
    else
        xf86DrvMsg(screen_info->scrnIndex, X_INFO, "EDID not found over DDC\n");

    clock_ranges = xcalloc(sizeof(ClockRange), 1);
    clock_ranges->minClock = 12000;
    clock_ranges->maxClock = 165000;
    clock_ranges->clockIndex = -1;
    clock_ranges->interlaceAllowed = FALSE;
    clock_ranges->doubleScanAllowed = FALSE;
    screen_info->progClock = TRUE;

    xf86ValidateModes(screen_info, screen_info->monitor->Modes,
                      screen_info->display->modes, clock_ranges, 0, 320, 2048,
                      16 * screen_info->bitsPerPixel, 200, 2047,
                      screen_info->display->virtualX,
                      screen_info->display->virtualY,
                      screen_info->videoRam, LOOKUP_BEST_REFRESH);
    xf86PruneDriverModes(screen_info);

    /* Set display resolution */
    xf86SetDpi(screen_info, 100, 100);

    if (screen_info->modes == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "No modes available\n");
        return FALSE;
    }

    screen_info->currentMode = screen_info->modes;

    /* options */
    xf86CollectOptions(screen_info, NULL);
    avivo->options = xalloc(sizeof(avivo_options));

    if (!avivo->options)
        return FALSE;

    memcpy(avivo->options, avivo_options, sizeof(avivo_options));
    xf86ProcessOptions(screen_info->scrnIndex, screen_info->options,
                       avivo->options);

#ifdef WITH_VGAHW
    xf86LoadSubModule(screen_info, "vgahw");

    vgaHWGetHWRec (screen_info);
    vgaHWGetIOBase(VGAHWPTR(screen_info));

#endif

    return TRUE;
}

static Bool
avivo_save_screen(ScreenPtr screen, int mode)
{
    Bool on = xf86IsUnblank(mode);

    if (on)
        SetTimeSinceLastInputEvent();

    return TRUE;
}

static void
avivo_save_cursor(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    state->cursor1_cntl = INREG(AVIVO_CURSOR1_CNTL);
    state->cursor1_location = INREG(AVIVO_CURSOR1_LOCATION);
    state->cursor1_size = INREG(AVIVO_CURSOR1_SIZE);
    state->cursor1_position = INREG(AVIVO_CURSOR1_POSITION);
}

static void
avivo_restore_cursor(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    OUTREG(AVIVO_CURSOR1_CNTL, state->cursor1_cntl);
    OUTREG(AVIVO_CURSOR1_LOCATION, state->cursor1_location);
    OUTREG(AVIVO_CURSOR1_SIZE, state->cursor1_size);
    OUTREG(AVIVO_CURSOR1_POSITION, state->cursor1_position);
}

static void
avivo_restore_state(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;

    OUTREG(0x330, state->clock_1);
    OUTREG(0x338, state->clock_2);
    OUTREG(AVIVO_CRTC1_H_TOTAL, state->crtc1_h_total);
    OUTREG(AVIVO_CRTC1_H_BLANK, state->crtc1_h_blank);
    OUTREG(AVIVO_CRTC1_H_SYNC_WID, state->crtc1_h_sync_wid);
    OUTREG(AVIVO_CRTC1_H_SYNC_POL, state->crtc1_h_sync_pol);
    OUTREG(AVIVO_CRTC1_V_TOTAL, state->crtc1_v_total);
    OUTREG(AVIVO_CRTC1_V_BLANK, state->crtc1_v_blank);
    OUTREG(AVIVO_CRTC1_V_SYNC_WID, state->crtc1_v_sync_wid);
    OUTREG(AVIVO_CRTC1_V_SYNC_POL, state->crtc1_v_sync_pol);
    OUTREG(AVIVO_CRTC1_CNTL, state->crtc1_cntl);
    OUTREG(AVIVO_CRTC1_MODE, state->crtc1_mode);
    OUTREG(0x60c0, state->crtc1_60c0_mystery);
    OUTREG(AVIVO_CRTC1_SCAN_ENABLE, state->crtc1_scan_enable);
    OUTREG(AVIVO_CRTC1_FB_FORMAT, state->crtc1_fb_format);
    OUTREG(AVIVO_CRTC1_FB_LOCATION, state->crtc1_fb_location);
    OUTREG(AVIVO_CRTC1_FB_END, state->crtc1_fb_end);
    OUTREG(AVIVO_CRTC1_PITCH, state->crtc1_pitch);
    OUTREG(AVIVO_CRTC1_X_LENGTH, state->crtc1_x_length);
    OUTREG(AVIVO_CRTC1_Y_LENGTH, state->crtc1_y_length);
    OUTREG(0x652c, state->crtc1_fb_height);
    OUTREG(AVIVO_CRTC1_OFFSET, state->crtc1_offset);
    OUTREG(AVIVO_CRTC1_EXPANSION_SOURCE, state->crtc1_expn_size);
    OUTREG(AVIVO_CRTC1_EXPANSION_CNTL, state->crtc1_expn_cntl);
    OUTREG(AVIVO_CRTC1_6594, state->crtc1_6594);
    OUTREG(AVIVO_CRTC1_659C, state->crtc1_659c);
    OUTREG(AVIVO_CRTC1_65A4, state->crtc1_65a4);
    OUTREG(AVIVO_CRTC1_65A8, state->crtc1_65a8);
    OUTREG(AVIVO_CRTC1_65AC, state->crtc1_65ac);
    OUTREG(AVIVO_CRTC1_65B0, state->crtc1_65b0);
    OUTREG(AVIVO_CRTC1_65B8, state->crtc1_65b8);
    OUTREG(AVIVO_CRTC1_65BC, state->crtc1_65bc);
    OUTREG(AVIVO_CRTC1_65C0, state->crtc1_65c0);
    OUTREG(AVIVO_CRTC1_65C8, state->crtc1_65c8);
    OUTREG(AVIVO_CRTC2_H_TOTAL, state->crtc2_h_total);
    OUTREG(AVIVO_CRTC2_H_BLANK, state->crtc2_h_blank);
    OUTREG(AVIVO_CRTC2_H_SYNC_WID, state->crtc2_h_sync_wid);
    OUTREG(AVIVO_CRTC2_H_SYNC_POL, state->crtc2_h_sync_pol);
    OUTREG(AVIVO_CRTC2_V_TOTAL, state->crtc2_v_total);
    OUTREG(AVIVO_CRTC2_V_BLANK, state->crtc2_v_blank);
    OUTREG(AVIVO_CRTC2_V_SYNC_WID, state->crtc2_v_sync_wid);
    OUTREG(AVIVO_CRTC2_V_SYNC_POL, state->crtc2_v_sync_pol);
    OUTREG(AVIVO_CRTC2_CNTL, state->crtc2_cntl);
    OUTREG(AVIVO_CRTC2_MODE, state->crtc2_mode);
    OUTREG(AVIVO_CRTC2_SCAN_ENABLE, state->crtc2_scan_enable);
    OUTREG(AVIVO_CRTC2_FB_FORMAT, state->crtc2_fb_format);
    OUTREG(AVIVO_CRTC2_FB_LOCATION, state->crtc2_fb_location);
    OUTREG(AVIVO_CRTC2_FB_END, state->crtc2_fb_end);
    OUTREG(AVIVO_CRTC2_PITCH, state->crtc2_pitch);
    OUTREG(AVIVO_CRTC2_X_LENGTH, state->crtc2_x_length);
    OUTREG(AVIVO_CRTC2_Y_LENGTH, state->crtc2_y_length);

    OUTREG(AVIVO_DAC1_CNTL, state->dac1_cntl);
    OUTREG(AVIVO_DAC1_MYSTERY1, state->dac1_mystery1);
    OUTREG(AVIVO_DAC1_MYSTERY2, state->dac1_mystery2);
    OUTREG(AVIVO_TMDS1_CNTL, state->tmds1_cntl);
    OUTREG(AVIVO_TMDS1_MYSTERY1, state->tmds1_mystery1);
    OUTREG(AVIVO_TMDS1_MYSTERY2, state->tmds1_mystery2);
    OUTREG(AVIVO_TMDS1_CLOCK_CNTL, state->tmds1_clock_cntl);
    OUTREG(AVIVO_TMDS1_MYSTERY3, state->tmds1_mystery3);
    OUTREG(AVIVO_DAC2_CNTL, state->dac2_cntl);
    OUTREG(AVIVO_DAC2_MYSTERY1, state->dac2_mystery1);
    OUTREG(AVIVO_DAC2_MYSTERY2, state->dac2_mystery2);
    OUTREG(AVIVO_TMDS2_CNTL, state->tmds2_cntl);
    OUTREG(AVIVO_TMDS2_MYSTERY1, state->tmds2_mystery1);
    OUTREG(AVIVO_TMDS2_MYSTERY2, state->tmds2_mystery2);
    OUTREG(AVIVO_TMDS2_CLOCK_CNTL, state->tmds2_clock_cntl);
    OUTREG(AVIVO_TMDS2_MYSTERY3, state->tmds2_mystery3);

    OUTREG(AVIVO_PLL_DIVIDER, state->pll_divider);
    OUTREG(AVIVO_PLL_POST_MUL, state->pll_post_mul);
    OUTREG(AVIVO_PLL_POST_DIV, state->pll_post_div);

#ifdef WITH_VGAHW
    vgaHWPtr hwp = VGAHWPTR(screen_info);
    vgaHWUnlock(hwp);
    vgaHWRestore(screen_info, &hwp->SavedReg, VGA_SR_MODE | VGA_SR_FONTS );
    vgaHWLock(hwp);
#endif

    avivo_restore_cursor(screen_info);
}    

static void
avivo_save_state(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_state *state = &avivo->saved_state;    

#ifdef WITH_VGAHW
    vgaHWPtr hwp = VGAHWPTR(screen_info);
    vgaHWUnlock(hwp);
    vgaHWSave(screen_info, &hwp->SavedReg, VGA_SR_MODE | VGA_SR_FONTS);
    vgaHWLock(hwp);
#endif

    avivo_save_cursor(screen_info);

    state->clock_1 = INREG(0x330);
    state->clock_2 = INREG(0x338);

    state->crtc1_h_total = INREG(AVIVO_CRTC1_H_TOTAL);

    state->crtc1_h_blank = INREG(AVIVO_CRTC1_H_BLANK);
    state->crtc1_h_sync_wid = INREG(AVIVO_CRTC1_H_SYNC_WID);
    state->crtc1_h_sync_pol = INREG(AVIVO_CRTC1_H_SYNC_POL);
    state->crtc1_v_total = INREG(AVIVO_CRTC1_V_TOTAL);
    state->crtc1_v_blank = INREG(AVIVO_CRTC1_V_BLANK);
    state->crtc1_v_sync_wid = INREG(AVIVO_CRTC1_V_SYNC_WID);
    state->crtc1_v_sync_pol = INREG(AVIVO_CRTC1_V_SYNC_POL);
    state->crtc1_cntl = INREG(AVIVO_CRTC1_CNTL);
    state->crtc1_mode = INREG(AVIVO_CRTC1_MODE);
    state->crtc1_60c0_mystery = INREG(0x60C0);
    state->crtc1_scan_enable = INREG(AVIVO_CRTC1_SCAN_ENABLE);
    state->crtc1_fb_format = INREG(AVIVO_CRTC1_FB_FORMAT);
    state->crtc1_fb_location = INREG(AVIVO_CRTC1_FB_LOCATION);
    state->crtc1_fb_end = INREG(AVIVO_CRTC1_FB_END);
    state->crtc1_pitch = INREG(AVIVO_CRTC1_PITCH);
    state->crtc1_x_length = INREG(AVIVO_CRTC1_X_LENGTH);
    state->crtc1_y_length = INREG(AVIVO_CRTC1_Y_LENGTH);
    state->crtc1_fb_height = INREG(0x652c);
    state->crtc1_offset = INREG(AVIVO_CRTC1_OFFSET);
    state->crtc1_expn_size = INREG(AVIVO_CRTC1_EXPANSION_SOURCE);
    state->crtc1_expn_cntl = INREG(AVIVO_CRTC1_EXPANSION_CNTL);
    state->crtc1_6594 = INREG(AVIVO_CRTC1_6594);
    state->crtc1_659c = INREG(AVIVO_CRTC1_659C);
    state->crtc1_65a4 = INREG(AVIVO_CRTC1_65A4);
    state->crtc1_65a8 = INREG(AVIVO_CRTC1_65A8);
    state->crtc1_65ac = INREG(AVIVO_CRTC1_65AC);
    state->crtc1_65b0 = INREG(AVIVO_CRTC1_65B0);
    state->crtc1_65b8 = INREG(AVIVO_CRTC1_65B8);
    state->crtc1_65bc = INREG(AVIVO_CRTC1_65BC);
    state->crtc1_65c0 = INREG(AVIVO_CRTC1_65C0);
    state->crtc1_65c8 = INREG(AVIVO_CRTC1_65C8);

    state->crtc2_h_total = INREG(AVIVO_CRTC2_H_TOTAL);
    state->crtc2_h_blank = INREG(AVIVO_CRTC2_H_BLANK);
    state->crtc2_h_sync_wid = INREG(AVIVO_CRTC2_H_SYNC_WID);
    state->crtc2_h_sync_pol = INREG(AVIVO_CRTC2_H_SYNC_POL);
    state->crtc2_v_total = INREG(AVIVO_CRTC2_V_TOTAL);
    state->crtc2_v_blank = INREG(AVIVO_CRTC2_V_BLANK);
    state->crtc2_v_sync_wid = INREG(AVIVO_CRTC2_V_SYNC_WID);
    state->crtc2_v_sync_pol = INREG(AVIVO_CRTC2_V_SYNC_POL);
    state->crtc2_cntl = INREG(AVIVO_CRTC2_CNTL);
    state->crtc2_mode = INREG(AVIVO_CRTC2_MODE);
    state->crtc2_scan_enable = INREG(AVIVO_CRTC2_SCAN_ENABLE);
    state->crtc2_fb_format = INREG(AVIVO_CRTC2_FB_FORMAT);
    state->crtc2_fb_location = INREG(AVIVO_CRTC2_FB_LOCATION);
    state->crtc2_fb_end = INREG(AVIVO_CRTC2_FB_END);
    state->crtc2_pitch = INREG(AVIVO_CRTC2_PITCH);
    state->crtc2_x_length = INREG(AVIVO_CRTC2_X_LENGTH);
    state->crtc2_y_length = INREG(AVIVO_CRTC2_Y_LENGTH);

    state->dac1_cntl = INREG(AVIVO_DAC1_CNTL);
    state->dac1_mystery1 = INREG(AVIVO_DAC1_MYSTERY1);
    state->dac1_mystery2 = INREG(AVIVO_DAC1_MYSTERY2);

    state->tmds1_cntl = INREG(AVIVO_TMDS1_CNTL);
    state->tmds1_mystery1 = INREG(AVIVO_TMDS1_MYSTERY1);
    state->tmds1_mystery2 = INREG(AVIVO_TMDS1_MYSTERY2);
    state->tmds1_clock_cntl = INREG(AVIVO_TMDS1_CLOCK_CNTL);
    state->tmds1_mystery3 = INREG(AVIVO_TMDS1_MYSTERY3);

    state->dac2_cntl = INREG(AVIVO_DAC2_CNTL);
    state->dac2_mystery1 = INREG(AVIVO_DAC2_MYSTERY1);
    state->dac2_mystery2 = INREG(AVIVO_DAC2_MYSTERY2);

    state->tmds2_cntl = INREG(AVIVO_TMDS2_CNTL);
    state->tmds2_mystery1 = INREG(AVIVO_TMDS2_MYSTERY1);
    state->tmds2_mystery2 = INREG(AVIVO_TMDS2_MYSTERY2);
    state->tmds2_clock_cntl = INREG(AVIVO_TMDS2_CLOCK_CNTL);
    state->tmds2_mystery3 = INREG(AVIVO_TMDS2_MYSTERY3);
    
    state->pll_divider = INREG(AVIVO_PLL_DIVIDER);
    state->pll_post_div = INREG(AVIVO_PLL_POST_DIV);
    state->pll_post_mul = INREG(AVIVO_PLL_POST_MUL);
}
    
static Bool
avivo_screen_init(int index, ScreenPtr screen, int argc, char **argv)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    VisualPtr visual;
    int flags;

    avivo_save_state(screen_info);

    /* set first video mode */
    if (!avivo_set_mode(screen_info, screen_info->currentMode))
        return FALSE;

    /* set the viewport */
    avivo_adjust_frame(index, screen_info->frameX0, screen_info->frameY0, 0);

    /* mi layer */
    miClearVisualTypes();

    if (!xf86SetDefaultVisual(screen_info, -1))
        return FALSE;

    if (!miSetVisualTypes(screen_info->depth, TrueColorMask,
                          screen_info->rgbBits, TrueColor))
        return FALSE;

    if (!miSetPixmapDepths())
        return FALSE;

    if (!fbScreenInit(screen, avivo->fb_base + screen_info->fbOffset,
                      screen_info->virtualX, screen_info->virtualY,
                      screen_info->xDpi, screen_info->yDpi,
                      screen_info->displayWidth, screen_info->bitsPerPixel))
        return FALSE;

    /* Fixup RGB ordering */
    visual = screen->visuals + screen->numVisuals;
    while (--visual >= screen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed = screen_info->offset.red;
            visual->offsetGreen = screen_info->offset.green;
            visual->offsetBlue = screen_info->offset.blue;
            visual->redMask  = screen_info->mask.red;
            visual->greenMask = screen_info->mask.green;
            visual->blueMask = screen_info->mask.blue;
        }
    }

    /* must be after RGB ordering fixed */
    fbPictureInit(screen, 0, 0);

    xf86SetBlackWhitePixels(screen);

    xf86DPMSInit(screen, avivo_dpms, 0);

    miDCInitialize(screen, xf86GetPointerScreenFuncs());
    /* FIXME enormous hack ... */
    avivo->cursor_offset = screen_info->virtualX * screen_info->virtualY * 4;
    avivo_cursor_init(screen);

    if (!miCreateDefColormap(screen))
        return FALSE;

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
        xf86ShowUnusedOptions(screen_info->scrnIndex, screen_info->options);

    screen->SaveScreen = avivo_save_screen;
    avivo->close_screen = screen->CloseScreen;
    screen->CloseScreen = avivo_close_screen;

    return TRUE;
}

static Bool
avivo_enter_vt(int index, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];

    avivo_save_state(screen_info);

    if (!avivo_set_mode(screen_info, screen_info->currentMode))
        return FALSE;

    avivo_restore_cursor(screen_info);
    avivo_adjust_frame(index, screen_info->frameX0, screen_info->frameY0, 0);

    return TRUE;
}

static void
avivo_leave_vt(int index, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];

    avivo_save_cursor(screen_info);
    avivo_restore_state(screen_info);
}

static void
avivo_enable_crtc(struct avivo_info *avivo, struct avivo_crtc *crtc,
                  int enable)
{
    int scan_enable, cntl;

    if (enable) {
        scan_enable = AVIVO_CRTC_SCAN_EN;
        cntl = 0x00010101;
    }
    else {
        scan_enable = 0;
        cntl = 0;
    }
    
    if (crtc->id == 1) {
        OUTREG(AVIVO_CRTC1_SCAN_ENABLE, scan_enable);
        OUTREG(AVIVO_CRTC1_CNTL, cntl);
    }
    else if (crtc->id == 2) {
        OUTREG(AVIVO_CRTC2_SCAN_ENABLE, scan_enable);
        OUTREG(AVIVO_CRTC2_CNTL, cntl);
    }

    avivo_wait_idle(avivo);
}

static void
avivo_enable_output(struct avivo_info *avivo, struct avivo_output *output,
                    int enable)
{
    int value1, value2, value3, value4, value5;

    avivo_wait_idle(avivo);

    if (output->type == TMDS) {
        value3 = 0x10000011;
        value5 = 0x00001010;

        if (enable) {
            value1 = AVIVO_TMDS_MYSTERY1_EN;
            value2 = AVIVO_TMDS_MYSTERY2_EN;
            value4 = 0x00001f1f;
            if (output->output_num == 2)
                value4 |= 0x00000020;
            value5 |= AVIVO_TMDS_EN;
        }
        else {
            value1 = 0x04000000;
            value2 = 0;
            value4 = 0x00060000;
        }

        if (output->output_num == 1) {
            OUTREG(AVIVO_TMDS1_MYSTERY1, value1);
            OUTREG(AVIVO_TMDS1_MYSTERY2, value2);
            OUTREG(AVIVO_TMDS1_MYSTERY3, value3);
            OUTREG(AVIVO_TMDS1_CLOCK_CNTL, value4);
            OUTREG(AVIVO_TMDS1_CNTL, value5);
        }
        else if (output->output_num == 2) {
            OUTREG(AVIVO_TMDS2_MYSTERY1, value1);
            OUTREG(AVIVO_TMDS2_MYSTERY2, value2);
            value3 |= 0x00630000;
            /* This needs to be set on TMDS, and unset on LVDS. */
            value3 |= INREG(AVIVO_TMDS2_MYSTERY3) & (1 << 29);
            OUTREG(AVIVO_TMDS2_MYSTERY3, value3);
            OUTREG(AVIVO_TMDS2_CLOCK_CNTL, value4);
            /* This needs to be set on LVDS, and unset on TMDS.  Luckily, the
             * BIOS appears to set it up for us, so just carry it over. */
            value5 |= INREG(AVIVO_TMDS2_CNTL) & (1 << 24);
            OUTREG(AVIVO_TMDS2_CNTL, value5);
        }
    }
    else if (output->type == VGA) {
        if (enable) {
            value1 = 0;
            value2 = 0;
            value3 = AVIVO_DAC_EN;
        }
        else {
            value1 = AVIVO_DAC_MYSTERY1_DIS;
            value2 = AVIVO_DAC_MYSTERY2_DIS;
            value3 = 0;
        }

        if (output->output_num == 1) {
            OUTREG(AVIVO_DAC1_MYSTERY1, value1);
            OUTREG(AVIVO_DAC1_MYSTERY2, value2);
            OUTREG(AVIVO_DAC1_CNTL, value3);
        }
        else if (output->output_num == 2) {
            OUTREG(AVIVO_DAC2_MYSTERY1, value1);
            OUTREG(AVIVO_DAC2_MYSTERY2, value2);
            OUTREG(AVIVO_DAC2_CNTL, value3);
        }
    }
}

static void
avivo_set_pll(struct avivo_info *avivo, struct avivo_crtc *crtc)
{
    int div, pdiv, pmul;
    int n_pdiv, n_pmul;
    int clock;
    int diff, n_diff;

    div = 1080000 / crtc->clock;
    pdiv = 2;
    pmul = floor(((40.0 * crtc->clock * pdiv * div) / 1080000.0) + 0.5);
    clock = (pmul * 108000) / (40 * pdiv * div);
    diff = fabsl(clock - crtc->clock);
    while (1) {
        n_pdiv = pdiv + 1;
        n_pmul = floor(((40.0 * crtc->clock * n_pdiv * div) / 1080000.0) + 0.5);
        clock = (n_pmul * 108000) / (40 * n_pdiv * div);
        n_diff = fabsl(clock - crtc->clock);
        if (n_diff >= diff)
            break;
        pdiv = n_pdiv;
        pmul = n_pmul;
    }
    ErrorF("pll: div %d, pmul 0x%X(%d), pdiv %d\n",
           div, pmul, pmul, pdiv);
    OUTREG(AVIVO_PLL_DIVIDER, div);
    OUTREG(AVIVO_PLL_POST_DIV, pdiv);
    OUTREG(AVIVO_PLL_POST_MUL, (pmul << AVIVO_PLL_POST_MUL_SHIFT));
}

static void
avivo_crtc_enable(struct avivo_info *avivo, struct avivo_crtc *crtc, int on)
{
    unsigned long fb_location = crtc->fb_offset + avivo->fb_addr;

    if (crtc->id == 1) {
        OUTREG(AVIVO_CRTC1_CNTL, 0);

        if (on) {
            /* Switch from text to graphics mode. */
            OUTREG(0x0330, 0x00010600);
            OUTREG(0x0338, 0x00000400);

            avivo_setup_cursor(avivo, 1, 1);

            OUTREG(AVIVO_CRTC1_FB_LOCATION, fb_location);
            OUTREG(AVIVO_CRTC1_FB_FORMAT, crtc->fb_format);
            OUTREG(AVIVO_CRTC1_FB_END, fb_location + crtc->fb_length);
            OUTREG(AVIVO_CRTC1_MODE, 0);
            OUTREG(0x60c0, 0);

            avivo_set_pll(avivo, crtc);

            OUTREG(0x652c, crtc->fb_height);
            OUTREG(AVIVO_CRTC1_EXPANSION_SOURCE, (crtc->fb_width << 16) |
                                                 crtc->fb_height);
            OUTREG(AVIVO_CRTC1_EXPANSION_CNTL, AVIVO_CRTC_EXPANSION_EN);

            OUTREG(AVIVO_CRTC1_659C, AVIVO_CRTC1_659C_VALUE);
            OUTREG(AVIVO_CRTC1_65A8, AVIVO_CRTC1_65A8_VALUE);
            OUTREG(AVIVO_CRTC1_65AC, AVIVO_CRTC1_65AC_VALUE);
            OUTREG(AVIVO_CRTC1_65B8, AVIVO_CRTC1_65B8_VALUE);
            OUTREG(AVIVO_CRTC1_65BC, AVIVO_CRTC1_65BC_VALUE);
            OUTREG(AVIVO_CRTC1_65C8, AVIVO_CRTC1_65C8_VALUE);
            OUTREG(AVIVO_CRTC1_6594, AVIVO_CRTC1_6594_VALUE);
            OUTREG(AVIVO_CRTC1_65A4, AVIVO_CRTC1_65A4_VALUE);
            OUTREG(AVIVO_CRTC1_65B0, AVIVO_CRTC1_65B0_VALUE);
            OUTREG(AVIVO_CRTC1_65C0, AVIVO_CRTC1_65C0_VALUE);

            OUTREG(AVIVO_CRTC1_X_LENGTH, crtc->fb_width);
            OUTREG(AVIVO_CRTC1_Y_LENGTH, crtc->fb_height);
            OUTREG(AVIVO_CRTC1_PITCH, crtc->fb_pitch);
            OUTREG(AVIVO_CRTC1_H_TOTAL, crtc->h_total);
            OUTREG(AVIVO_CRTC1_H_BLANK, crtc->h_blank);
            OUTREG(AVIVO_CRTC1_H_SYNC_WID, crtc->h_sync_wid);
            OUTREG(AVIVO_CRTC1_H_SYNC_POL, crtc->h_sync_pol);
            OUTREG(AVIVO_CRTC1_V_TOTAL, crtc->v_total);
            OUTREG(AVIVO_CRTC1_V_BLANK, crtc->v_blank);
            OUTREG(AVIVO_CRTC1_V_SYNC_WID, crtc->v_sync_wid);
            OUTREG(AVIVO_CRTC1_V_SYNC_POL, crtc->v_sync_pol);

            OUTREG(AVIVO_CRTC1_CNTL, 0x00010101);
            OUTREG(AVIVO_CRTC1_SCAN_ENABLE, AVIVO_CRTC_SCAN_EN);
        }
    }
}

static void
avivo_setup_crtc(struct avivo_info *avivo, struct avivo_crtc *crtc,
                 DisplayModePtr mode)
{
    ErrorF("mode: hdisp %d, htotal %d, hss %d, hse %d, hsk %d\n",
           mode->HDisplay, mode->HTotal, mode->HSyncStart, mode->HSyncEnd,
           mode->HSkew);
    ErrorF("      vdisp %d, vtotal %d, vss %d, vse %d, vsc %d\n",
           mode->VDisplay, mode->VTotal, mode->VSyncStart, mode->VSyncEnd,
           mode->VScan);

    crtc->h_total = mode->HTotal - 1;
    crtc->h_blank = (mode->HTotal - mode->HSyncStart) << 16 |
                    (mode->HTotal - mode->HSyncStart + mode->HDisplay);
    crtc->h_sync_wid = (mode->HSyncEnd - mode->HSyncStart) << 16;
    crtc->h_sync_pol = 0;
    crtc->v_total = mode->VTotal - 1;
    crtc->v_blank = (mode->VTotal - mode->VSyncStart) << 16 |
                    (mode->VTotal - mode->VSyncStart + mode->VDisplay);
    crtc->v_sync_wid = (mode->VSyncEnd - mode->VSyncStart) << 16;

    crtc->clock = mode->Clock;

    crtc->fb_width = mode->HDisplay;
    crtc->fb_height = mode->VDisplay;
    crtc->fb_pitch = mode->HDisplay;
    crtc->fb_format = AVIVO_CRTC_FORMAT_ARGB32;
    crtc->fb_offset = 0;
    crtc->fb_length = crtc->fb_pitch * crtc->fb_height * 4;

    avivo_crtc_enable(avivo, crtc, 1);
}

static Bool
avivo_switch_mode(int index, DisplayModePtr mode, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_crtc *crtc = avivo->crtcs;
    struct avivo_output *output = avivo->outputs;
    Bool ret;

    /* FIXME: First CRTC hardcoded ... */
    avivo_setup_crtc(avivo, crtc, mode);

    while (output) {
        /* FIXME: CRTC <-> Output association. */
        avivo_enable_output(avivo, output, 1);
        output = output->next;
    }

    return TRUE;
}

/* Set a graphics mode */
static Bool
avivo_set_mode(ScrnInfoPtr screen_info, DisplayModePtr mode)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

    avivo_switch_mode(screen_info->scrnIndex, mode, 0);
    screen_info->vtSema = TRUE;

    return TRUE;
}

static void
avivo_adjust_frame(int index, int x, int y, int flags)
{
    struct avivo_info *avivo = avivo_get_info(xf86Screens[index]);

    OUTREG(AVIVO_CRTC1_OFFSET, (x << 16) | y);
}

static void
avivo_free_screen(int index, int flags)
{
    avivo_free_info(xf86Screens[index]);
}

static Bool
avivo_close_screen(int index, ScreenPtr screen)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    Bool ret;

    avivo_restore_state(screen_info);
    
    screen->CloseScreen = avivo->close_screen;
    return screen->CloseScreen(index, screen);
}

static void
avivo_dpms(ScrnInfoPtr screen_info, int mode, int flags)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_crtc *crtc = avivo->crtcs;
    struct avivo_output *output = avivo->outputs;
    int enable = (mode == DPMSModeOn);

    if (!screen_info->vtSema)
        return;

    while (output) {
        avivo_enable_output(avivo, output, enable);
        output = output->next;
    }

    /* FIXME: First CRTC hardcoded. */
    avivo_enable_crtc(avivo, crtc, enable);
}
