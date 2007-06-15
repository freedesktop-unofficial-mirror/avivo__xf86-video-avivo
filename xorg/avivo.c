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

static void avivo_dpms(ScrnInfoPtr screen_info, int mode, int flags);

#ifdef PCIACCESS
static const struct pci_id_match avivo_device_match[] = {
    {
        PCI_VENDOR_ATI, 0x71c2, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0
    },
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
    { PCI_CHIP_RV530_71C2, "RV530 (Radeon X1600)" },
    { PCI_CHIP_RV530_71C5, "RV530 (Radeon X1600)" },
    { PCI_CHIP_R580_724B,  "R580 (Radeon X1900 GT)" },
    { -1,                  NULL }
};

static PciChipsets avivo_pci_chips[] = {
  { PCI_CHIP_RV530_71C2, PCI_CHIP_RV530_71C2, RES_SHARED_VGA },
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
avivo_get_chipset(struct avivo_info *avivo)
{
    switch (avivo->pci_info->chipType) {
    case PCI_CHIP_RV530_71C2:
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
    xf86CrtcConfigPtr config;
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

#ifdef AVIVO_RR12
    if (!avivo_crtc_create(screen_info))
        return FALSE;
#if 0
    if (!avivo_output_setup(screen_info))
        return FALSE;
#else
    avivo_output_setup(screen_info);
#endif
    if (!xf86InitialConfiguration(screen_info, FALSE)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "No valid modes.\n");
        return FALSE;
    }

    if (!xf86SetDepthBpp(screen_info, 0, 0, 0, Support32bppFb))
        return FALSE;
    xf86PrintDepthBpp(screen_info);
    switch (screen_info->depth) {
    case 16:
        avivo->bpp = 2;
        break;
    case 24:
    case 32:
        avivo->bpp = 4;
        break;
    default:
        FatalError("Unsupported screen depth: %d\n", xf86GetDepth());
    }
    /* color weight */
    if (!xf86SetWeight(screen_info, rzeros, rzeros))
        return FALSE;
    /* visual init */
    if (!xf86SetDefaultVisual(screen_info, -1))
        return FALSE;
    /* TODO: gamma correction */
    xf86SetGamma(screen_info, gzeros);
    /* Set display resolution */
    xf86SetDpi(screen_info, 100, 100);
#if 0
    /* probe monitor found */
    monitor = NULL;
    config = XF86_CRTC_CONFIG_PTR(screen_info);
    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        struct avivo_output_private *avivo_output = output->driver_private;
        if (output->funcs->detect(output) == XF86OutputStatusConnected) {
            output->funcs->get_modes(output);
            monitor = output->MonInfo;
            xf86PrintEDID(monitor);
        }
    }

    if (monitor == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "No monitor found.\n");
        return FALSE;
    }
    xf86SetDDCproperties(screen_info, monitor);
    /* validates mode */
    clock_ranges = xcalloc(sizeof(ClockRange), 1);
    if (clock_ranges == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Failed to allocate memory for clock range\n");
        return FALSE;
    }
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
#endif
    /* check if there modes available */
    if (!xf86RandR12PreInit(screen_info)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "RandR initialization failure\n");
        return FALSE;
    }	
    if (screen_info->modes == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "No modes available\n");
        return FALSE;
    }
    screen_info->currentMode = screen_info->modes;
#else
    /* probe BIOS information */
    avivo_probe_info(screen_info);
    if (!xf86SetDepthBpp(screen_info, 0, 0, 0, Support32bppFb))
        return FALSE;
    xf86PrintDepthBpp(screen_info);
    switch (screen_info->depth) {
    case 16:
        avivo->bpp = 2;
        break;
    case 24:
    case 32:
        avivo->bpp = 4;
        break;
    default:
        FatalError("Unsupported screen depth: %d\n", xf86GetDepth());
    }
    /* color weight */
    if (!xf86SetWeight(screen_info, rzeros, rzeros))
        return FALSE;
    /* visual init */
    if (!xf86SetDefaultVisual(screen_info, -1))
        return FALSE;
    xf86SetGamma(screen_info, gzeros);
#if 1
    avivo_probe_monitor(screen_info);
    if (avivo->connector_default && avivo->connector_default->monitor)
        xf86SetDDCproperties(screen_info,
                             xf86PrintEDID(avivo->connector_default->monitor));
    else
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "EDID not found over DDC\n");
#else
    avivo_i2c_init(screen_info);
    screen_info->monitor = screen_info->confScreen->monitor;
    monitor = avivo_ddc(screen_info);
    if (monitor)
        xf86SetDDCproperties(screen_info, xf86PrintEDID(monitor));
    else
        xf86DrvMsg(screen_info->scrnIndex, X_INFO,
                   "EDID not found over DDC\n");
#endif
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
    xf86SetDpi(screen_info, 100, 100);

    if (screen_info->modes == NULL) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "No modes available\n");
        return FALSE;
    }
    screen_info->currentMode = screen_info->modes;
#endif

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

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "[ScreenPreInit OK]\n");
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

    
static Bool
avivo_screen_init(int index, ScreenPtr screen, int argc, char **argv)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(screen_info);
    VisualPtr visual;
    int flags, i;
    unsigned int mc_memory_map;
    unsigned int mc_memory_map_end;

    avivo_save_state(screen_info);

    /* init gpu memory mapping */
    mc_memory_map = (avivo->fb_addr >> 16) & AVIVO_MC_MEMORY_MAP_BASE_MASK;
    mc_memory_map_end = ((avivo->fb_addr + avivo->fb_size) >> 16) - 1;
    mc_memory_map |= (mc_memory_map_end << AVIVO_MC_MEMORY_MAP_END_SHIFT)
        & AVIVO_MC_MEMORY_MAP_END_MASK;
    radeon_set_mc(screen_info, AVIVO_MC_MEMORY_MAP, mc_memory_map);
    OUTREG(AVIVO_VGA_MEMORY_BASE,
           (avivo->fb_addr >> 16) & AVIVO_MC_MEMORY_MAP_BASE_MASK);
    OUTREG(AVIVO_VGA_FB_START, avivo->fb_addr);

#ifdef AVIVO_RR12
    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        /* Mark that we'll need to re-set the mode for sure */
        memset(&crtc->mode, 0, sizeof(crtc->mode));
        if (!crtc->desiredMode.CrtcHDisplay) {
            memcpy(&crtc->desiredMode, screen_info->currentMode,
                   sizeof(crtc->desiredMode));
            crtc->desiredRotation = RR_Rotate_0;
            crtc->desiredX = 0;
            crtc->desiredY = 0;
        }

        if (!xf86CrtcSetMode (crtc, &crtc->desiredMode, crtc->desiredRotation,
                              crtc->desiredX, crtc->desiredY))
            return FALSE;
    }
#else
    /* set first video mode */
    if (!avivo_set_mode(screen_info, screen_info->currentMode))
        return FALSE;
#endif
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
#if 1
    /* FIXME enormous hack ... */
    avivo->cursor_offset = screen_info->virtualX * screen_info->virtualY * 4;
    avivo_cursor_init(screen);
#endif

    if (!miCreateDefColormap(screen))
        return FALSE;

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
        xf86ShowUnusedOptions(screen_info->scrnIndex, screen_info->options);

    screen->SaveScreen = avivo_save_screen;
    avivo->close_screen = screen->CloseScreen;
    screen->CloseScreen = avivo_close_screen;

#ifdef AVIVO_RR12
    xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "Should not be here\n");
    if (!xf86CrtcScreenInit(screen))
        return FALSE;
#endif

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "[ScreenInit OK]\n");
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
    
    if (crtc->id == 0) {
        OUTREG(AVIVO_CRTC1_SCAN_ENABLE, scan_enable);
        OUTREG(AVIVO_CRTC1_CNTL, cntl);
    }
    else if (crtc->id == 1) {
        OUTREG(AVIVO_CRTC2_SCAN_ENABLE, scan_enable);
        OUTREG(AVIVO_CRTC2_CNTL, cntl);
    }

    avivo_wait_idle(avivo);
}

static void
avivo_enable_output(struct avivo_info *avivo,
                    struct avivo_connector *connector,
                    struct avivo_output *output,
                    struct avivo_crtc *crtc,
                    int enable)
{
    int value1, value2, value3, value4, value5;

    avivo_wait_idle(avivo);
    output->is_enabled = enable;

    if (output->type == OUTPUT_TMDS) {
        value3 = 0x10000011;
        value5 = 0x00001010;

        if (enable) {
            value1 = AVIVO_TMDS_MYSTERY1_EN;
            value2 = AVIVO_TMDS_MYSTERY2_EN;
            value4 = 0x00001f1f;
            if (connector->connector_num == 1)
                value4 |= 0x00000020;
            value5 |= AVIVO_TMDS_EN;
        }
        else {
            value1 = 0x04000000;
            value2 = 0;
            value4 = 0x00060000;
        }

        if (connector->connector_num == 0) {
            OUTREG(AVIVO_TMDS1_CRTC_SOURCE, crtc->id);
            OUTREG(AVIVO_TMDS1_MYSTERY1, value1);
            OUTREG(AVIVO_TMDS1_MYSTERY2, value2);
            OUTREG(AVIVO_TMDS1_MYSTERY3, value3);
            OUTREG(AVIVO_TMDS1_CLOCK_CNTL, value4);
            OUTREG(AVIVO_TMDS1_CNTL, value5);
        }
        else if (connector->connector_num == 1
                 || connector->connector_num == 2) {
            OUTREG(AVIVO_TMDS2_CRTC_SOURCE, crtc->id);
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
    else if (output->type == OUTPUT_DAC) {
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

        if (connector->connector_num == 0) {
            OUTREG(AVIVO_DAC1_CRTC_SOURCE, crtc->id);
            OUTREG(AVIVO_DAC1_MYSTERY1, value1);
            OUTREG(AVIVO_DAC1_MYSTERY2, value2);
            OUTREG(AVIVO_DAC1_CNTL, value3);
        }
        else if (connector->connector_num == 1) {
            OUTREG(AVIVO_DAC2_CRTC_SOURCE, crtc->id);
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
    clock = (pmul * 1080000) / (40 * pdiv * div);
    diff = fabsl(clock - crtc->clock);
    while (1) {
        n_pdiv = pdiv + 1;
        n_pmul = floor(((40.0 * crtc->clock * n_pdiv * div) / 1080000.0) + 0.5);
        clock = (n_pmul * 1080000) / (40 * n_pdiv * div);
        n_diff = fabsl(clock - crtc->clock);
        if (n_diff >= diff)
            break;
        pdiv = n_pdiv;
        pmul = n_pmul;
        diff = n_diff;
    }
    clock = (pmul * 1080000) / (40 * pdiv * div);
    ErrorF("clock: %d requested: %d\n", clock, crtc->clock);
    ErrorF("pll: div %d, pmul 0x%X(%d), pdiv %d\n",
           div, pmul, pmul, pdiv);

    OUTREG(AVIVO_PLL_CNTL, 0);
    OUTREG(AVIVO_PLL_DIVIDER, div);
    OUTREG(AVIVO_PLL_DIVIDER_CNTL, AVIVO_PLL_EN);
    OUTREG(AVIVO_PLL_POST_DIV, pdiv);
    OUTREG(AVIVO_PLL_POST_MUL, (pmul << AVIVO_PLL_POST_MUL_SHIFT));
    OUTREG(AVIVO_PLL_CNTL, AVIVO_PLL_EN);
}

static void
avivo_crtc_enable(struct avivo_info *avivo, struct avivo_crtc *crtc, int on)
{
    unsigned long fb_location = crtc->fb_offset + avivo->fb_addr;

    if (crtc->id == 0) {
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
            OUTREG(AVIVO_CRTC1_60c0_MYSTERY, 0);

            avivo_set_pll(avivo, crtc);

            OUTREG(AVIVO_CRTC1_FB_HEIGHT, crtc->fb_height);
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
#ifdef AVIVO_RR12
static Bool
avivo_switch_mode(int index, DisplayModePtr mode, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    Bool ok;

    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "set mode: hdisp %d, htotal %d, hss %d, hse %d, hsk %d\n",
               mode->HDisplay, mode->HTotal, mode->HSyncStart, mode->HSyncEnd,
               mode->HSkew);
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "      vdisp %d, vtotal %d, vss %d, vse %d, vsc %d\n",
               mode->VDisplay, mode->VTotal, mode->VSyncStart, mode->VSyncEnd,
               mode->VScan);

    ok = xf86SetSingleMode(screen_info, mode, RR_Rotate_0);
    if (!ok) {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO, "Failed to set mode\n");
    } else {
        xf86DrvMsg(screen_info->scrnIndex, X_INFO, "Setting mode succeed\n");
    }
    return ok;
}
#else
static Bool
avivo_switch_mode(int index, DisplayModePtr mode, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    struct avivo_crtc *crtc = avivo->crtcs;
    struct avivo_output *output;
    struct avivo_connector *connector = avivo->connectors;
    Bool ret;

    /* FIXME: First CRTC hardcoded ... */
    avivo_setup_crtc(avivo, crtc, mode);

#if 0
    while (connector) {
        /* FIXME: CRTC <-> Output association. */
        output = connector->outputs;
        while (output) {
            avivo_enable_output(avivo, connector, output, crtc, 1);
            output = output->next;
        }
        connector = connector->next;
    }
#else
    output = avivo->connector_default->outputs;
    while (output) {
        avivo_enable_output(avivo, avivo->connector_default, output, crtc, 1);
        output = output->next;
    }
#endif

    return TRUE;
}
#endif

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
    struct avivo_connector *connector = avivo->connectors;
    struct avivo_output *output;
    int enable = (mode == DPMSModeOn);

    if (!screen_info->vtSema)
        return;

    while (connector) {
        output = connector->outputs;
        while (output) {
            avivo_enable_output(avivo, connector, output, crtc, enable);
            output = output->next;
        }
        connector = connector->next;
    }
    /* FIXME: First CRTC hardcoded. */
    avivo_enable_crtc(avivo, crtc, enable);
}
