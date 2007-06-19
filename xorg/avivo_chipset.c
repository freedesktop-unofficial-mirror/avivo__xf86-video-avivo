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

#ifdef PCIACCESS
const struct pci_id_match avivo_device_match[] = {
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
    {
        PCI_VENDOR_ATI, 0x7149, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0      
    },
    {
        PCI_VENDOR_ATI, 0x71C6, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0x00030000, 0x00ffffff, 0      
    }, 

    { 0, 0, 0 },
};
#endif

/* Supported chipsets.  I'm really, really glad that these are
 * separate, and the nomenclature is beyond reproach. */
SymTabRec avivo_chips[] = {
    { PCI_CHIP_RV515_7142, "RV515 (Radeon X1300)" },
    { PCI_CHIP_RV530_71C2, "RV530 (Radeon X1600)" },
    { PCI_CHIP_RV530_71C5, "RV530 (Radeon X1600)" },
    { PCI_CHIP_R580_724B,  "R580 (Radeon X1900 GT)" },
    { PCI_CHIP_RV530_71C6, "RV530 (Radeon X1650 Pro)" },
    { PCI_CHIP_M52_7149,   "M52 (Mobility Radeon X1300)" },
    { -1,                  NULL }
};

PciChipsets avivo_pci_chips[] = {
  { PCI_CHIP_RV530_71C2, PCI_CHIP_RV530_71C2, RES_SHARED_VGA },
  { PCI_CHIP_RV530_71C5, PCI_CHIP_RV530_71C5, RES_SHARED_VGA },
  { PCI_CHIP_R580_724B,  PCI_CHIP_R580_724B,  RES_SHARED_VGA },
  { PCI_CHIP_RV515_7142, PCI_CHIP_RV515_7142, RES_SHARED_VGA },
  { PCI_CHIP_M52_7149,   PCI_CHIP_M52_7149,   RES_SHARED_VGA },
  { PCI_CHIP_RV530_71C6, PCI_CHIP_RV530_71C6, RES_SHARED_VGA },
  { -1,                  -1,                  RES_UNDEFINED }
};

void
avivo_get_chipset(struct avivo_info *avivo)
{
    switch (avivo->pci_info->chipType) {
    case PCI_CHIP_RV530_71C2:
    case PCI_CHIP_RV530_71C6:
    case PCI_CHIP_RV530_71C5:
        avivo->chipset = CHIP_FAMILY_RV530;
        break;

    case PCI_CHIP_R580_724B:
        avivo->chipset = CHIP_FAMILY_R580;    
        break;

    case PCI_CHIP_M52_7149:
    case PCI_CHIP_RV515_7142:
        avivo->chipset = CHIP_FAMILY_RV515;    
        break;

    default:
        FatalError("Unknown chipset for %x!\n", avivo->pci_info->device);
        break;
    }
}
