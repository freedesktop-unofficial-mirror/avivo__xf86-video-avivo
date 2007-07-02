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
#define AVIVO_RR12 1
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "micmap.h"
#include "cursorstr.h"
#include "xf86Cursor.h"
#include "xf86str.h"
#include "xf86RandR12.h"
#include "xf86fbman.h"

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
static void avivo_adjust_frame(int index, int x, int y, int flags);
static void avivo_free_screen(int index, int flags);
static void avivo_free_info(ScrnInfoPtr screen_info);

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
                   "Couldn't map control memory at %p", (void *)avivo->ctrl_addr);
        return 0;
    }
    return 1;
}

static int
avivo_map_fb_mem(ScrnInfoPtr screen_info)
{
    struct avivo_info *avivo = avivo_get_info(screen_info);

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
                   "Couldn't map fb memory at %p", (void *)avivo->fb_addr);
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
}

/*
 * This function is called once for each screen at the start of the first
 * server generation to initialise the screen for all server generations.
 */
static Bool
avivo_preinit(ScrnInfoPtr screen_info, int flags)
{
    struct avivo_info *avivo;
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
               "Control memory at %p[size = %d, 0x%08X]\n",
               (void *)avivo->ctrl_addr, avivo->ctrl_size, avivo->ctrl_size);
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "Frame buffer memory at %p[size = %d, 0x%08X]\n",
               (void *)avivo->fb_addr, avivo->fb_size, avivo->fb_size);

    avivo_get_chipset(avivo);
    screen_info->chipset = "avivo";
    screen_info->monitor = screen_info->confScreen->monitor;

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

    if (!avivo_crtc_create(screen_info))
        return FALSE;

    /* options */
    xf86CollectOptions(screen_info, NULL);
    avivo->options = xalloc(sizeof(avivo_options));
    if (avivo->options == NULL)
        return FALSE;
    memcpy(avivo->options, avivo_options, sizeof(avivo_options));
    xf86ProcessOptions(screen_info->scrnIndex, screen_info->options,
                       avivo->options);

    avivo_output_setup(screen_info);
    if (!xf86InitialConfiguration(screen_info, FALSE)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR, "No valid modes.\n");
        return FALSE;
    }
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

#ifdef WITH_VGAHW
    xf86LoadSubModule(screen_info, "vgahw");
    vgaHWGetHWRec (screen_info);
    vgaHWGetIOBase(VGAHWPTR(screen_info));
#endif
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "pre-initialization successfull\n");
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
avivo_init_fb_manager(ScreenPtr screen,  BoxPtr fb_box)
{
    ScrnInfoPtr screen_info = xf86Screens[screen->myNum];
    RegionRec screen_region;
    RegionRec full_region;
    BoxRec screen_box;
    Bool ret;

    screen_box.x1 = 0;
    screen_box.y1 = 0;
    screen_box.x2 = screen_info->displayWidth;
    if (screen_info->virtualX > screen_info->virtualY)
        screen_box.y2 = screen_info->virtualX;
    else
        screen_box.y2 = screen_info->virtualY;

    if((fb_box->x1 >  screen_box.x1) || (fb_box->y1 >  screen_box.y1) ||
       (fb_box->x2 <  screen_box.x2) || (fb_box->y2 <  screen_box.y2)) {
        return FALSE;   
    }

    if (fb_box->y2 < fb_box->y1) return FALSE;
    if (fb_box->x2 < fb_box->x2) return FALSE;

    REGION_INIT(screen, &screen_region, &screen_box, 1); 
    REGION_INIT(screen, &full_region, fb_box, 1); 

    REGION_SUBTRACT(screen, &full_region, &full_region, &screen_region);

    ret = xf86InitFBManagerRegion(screen, &full_region);

    REGION_UNINIT(screen, &screen_region);
    REGION_UNINIT(screen, &full_region);
    
    return ret;
}
    
static Bool
avivo_screen_init(int index, ScreenPtr screen, int argc, char **argv)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(screen_info);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(screen_info);
    VisualPtr visual;
    int i;
    unsigned int mc_memory_map;
    unsigned int mc_memory_map_end;

    avivo_save_state(screen_info);

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
    /* fb memory box */
#if 0
    memset(&avivo->fb_memory_box, 0, sizeof(avivo->fb_memory_box));
    avivo->fb_memory_box.x1 = 0;
    avivo->fb_memory_box.x2 = screen_info->displayWidth;
    avivo->fb_memory_box.y1 = 0;
    avivo->fb_memory_box.y2 = screen_info->virtualY;
    if (!avivo_init_fb_manager(screen, &avivo->fb_memory_box)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't init fb manager\n");
        return FALSE;
    }
#endif
    /* display width is the higher resolution from width & height */
    if (screen_info->virtualX > screen_info->displayWidth)
        screen_info->displayWidth = screen_info->virtualX;
    /* display width * bpp need to be a multiple of 256 */
    screen_info->displayWidth = ceil(screen_info->displayWidth * avivo->bpp
		    / 256.0) * 256 / avivo->bpp;
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "padded display width %d\n", screen_info->displayWidth);
    /* mi layer */
    miClearVisualTypes();
    if (!xf86SetDefaultVisual(screen_info, -1)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't set default visual\n");
        return FALSE;
    }
    if (!miSetVisualTypes(screen_info->depth, TrueColorMask,
                          screen_info->rgbBits, TrueColor)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't set visual types\n");
        return FALSE;
    }
    if (!miSetPixmapDepths()) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't set pixmap depth\n");
        return FALSE;
    }
    ErrorF("VirtualX,Y %d, %d\n",
           screen_info->virtualX, screen_info->virtualY);
    if (!fbScreenInit(screen, avivo->fb_base + screen_info->fbOffset,
                      screen_info->virtualX, screen_info->virtualY,
                      screen_info->xDpi, screen_info->yDpi,
                      screen_info->displayWidth, screen_info->bitsPerPixel)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't init fb\n");
        return FALSE;
    }
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
                              crtc->desiredX, crtc->desiredY)) {
            xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                       "Couldn't set crtc mode\n");
            return FALSE;
        }
    }
    /* set the viewport */
    avivo_adjust_frame(index, screen_info->frameX0, screen_info->frameY0, 0);

    xf86DPMSInit(screen, xf86DPMSSet, 0);

    miDCInitialize(screen, xf86GetPointerScreenFuncs());

    if (!miCreateDefColormap(screen)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't create colormap\n");
        return FALSE;
    }

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
        xf86ShowUnusedOptions(screen_info->scrnIndex, screen_info->options);

    screen->SaveScreen = avivo_save_screen;
    avivo->close_screen = screen->CloseScreen;
    screen->CloseScreen = avivo_close_screen;

    if (!xf86CrtcScreenInit(screen)) {
        xf86DrvMsg(screen_info->scrnIndex, X_ERROR,
                   "Couldn't initialize crtc\n");
        return FALSE;
    }

    xf86DrvMsg(screen_info->scrnIndex, X_INFO, "initialization successfull\n");
    return TRUE;
}

static Bool
avivo_enter_vt(int index, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];

    avivo_save_state(screen_info);

    screen_info->vtSema = TRUE;
    if (!xf86SetDesiredModes(screen_info))
        return FALSE;
    avivo_adjust_frame(index, screen_info->frameX0, screen_info->frameY0, 0);

    return TRUE;
}

static void
avivo_leave_vt(int index, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];

    avivo_restore_state(screen_info);
}

static Bool
avivo_switch_mode(int index, DisplayModePtr mode, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];

    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "set mode: hdisp %d, htotal %d, hss %d, hse %d, hsk %d\n",
               mode->HDisplay, mode->HTotal, mode->HSyncStart, mode->HSyncEnd,
               mode->HSkew);
    xf86DrvMsg(screen_info->scrnIndex, X_INFO,
               "      vdisp %d, vtotal %d, vss %d, vse %d, vsc %d\n",
               mode->VDisplay, mode->VTotal, mode->VSyncStart, mode->VSyncEnd,               mode->VScan);

    return xf86SetSingleMode (screen_info, mode, RR_Rotate_0);
}

static void
avivo_adjust_frame(int index, int x, int y, int flags)
{
    ScrnInfoPtr screen_info = xf86Screens[index];
    struct avivo_info *avivo = avivo_get_info(xf86Screens[index]);
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(screen_info);
    xf86OutputPtr output = config->output[config->compat_output];
    xf86CrtcPtr crtc = output->crtc;
    struct avivo_crtc_private *avivo_crtc = crtc->driver_private;

    if (crtc && crtc->enabled) {
        OUTREG(AVIVO_CRTC1_OFFSET + avivo_crtc->crtc_offset, (x << 16) | y);
        crtc->x = output->initial_x + x;
        crtc->y = output->initial_y + y;
    }
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

    avivo_restore_state(screen_info);
    avivo_unmap_ctrl_mem(screen_info);
    avivo_unmap_fb_mem(screen_info);

    screen->CloseScreen = avivo->close_screen;
    return screen->CloseScreen(index, screen);
}
