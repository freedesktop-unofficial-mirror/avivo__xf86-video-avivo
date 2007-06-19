/*
 * Copyright © 2007 Daniel Stone
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
 * Author: Daniel Stone <daniel@fooishbar.org>
 *         Matthew Garrett <mjg59@srcf.ucam.org>
 *         Jerome Glisse <glisse@freedesktop.org>
 */
#ifndef _AVIVO_CHIPSET_H_
#define _AVIVO_CHIPSET_H_

#include "xf86_OSproc.h"
#include "xf86Resources.h"
/* Drivers for PCI hardware need this */
#include "xf86PciInfo.h"
/* Drivers that need to access the PCI config space directly need this */
#include "xf86Pci.h"
#ifdef PCIACCESS
#include <pciaccess.h>
#endif

#define PCI_CHIP_RV515_7142     0x7142
#define PCI_CHIP_RV530_71C2     0x71C2
#define PCI_CHIP_RV530_71C5     0x71C5
#define PCI_CHIP_R580_724B      0x724B
#define PCI_CHIP_M52_7149       0x7149
#define PCI_CHIP_RV530_71C6     0x71C6

enum avivo_chip_type {
    CHIP_FAMILY_RV515,
    CHIP_FAMILY_R520,
    CHIP_FAMILY_RV530,
    CHIP_FAMILY_R580,
    CHIP_FAMILY_LAST,
};

#ifdef PCIACCESS
const struct pci_id_match *avivo_device_match;
#endif
SymTabRec avivo_chips[];
PciChipsets avivo_pci_chips[];

#endif
