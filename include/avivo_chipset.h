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

#define PCI_CHIP_R520_7100	0x7100
#define PCI_CHIP_R520_M58_7102	0x7102
#define PCI_CHIP_R520_M58_7103	0x7103
#define PCI_CHIP_R520_7104	0x7104
#define PCI_CHIP_R520_7105	0x7105
#define PCI_CHIP_R520_M58_7106	0x7106
#define PCI_CHIP_R520_M58_7108	0x7108
#define PCI_CHIP_R520_7109	0x7109
#define PCI_CHIP_R520_710A	0x710A
#define PCI_CHIP_R520_710B	0x710B
#define PCI_CHIP_R520_710C	0x710C
#define PCI_CHIP_RV515_7140	0x7140
#define PCI_CHIP_RV515_PRO_7142	0x7142
#define PCI_CHIP_RV505_7143	0x7143
#define PCI_CHIP_RV515_7145	0x7145
#define PCI_CHIP_RV515_7146	0x7146
#define PCI_CHIP_RV505_7147	0x7147
#define PCI_CHIP_RV515_M52_7149	0x7149
#define PCI_CHIP_RV515_M52_714A	0x714A
#define PCI_CHIP_RV515_M52_714B	0x714B
#define PCI_CHIP_RV515_M52_714C	0x714C
#define PCI_CHIP_RV515_714D	0x714D
#define PCI_CHIP_RV515_LE_714E	0x714E
#define PCI_CHIP_RV515_GL_7152	0x7152
#define PCI_CHIP_RV515_GL_7153	0x7153
#define PCI_CHIP_RV515_715E	0x715E
#define PCI_CHIP_RV505_CE_715F	0x715F
#define PCI_CHIP_RV516_7180	0x7180
#define PCI_CHIP_RV516_XT_7181	0x7181
#define PCI_CHIP_RV516_7183	0x7183
#define PCI_CHIP_RV516_7187	0x7187
#define PCI_CHIP_RV515_M64_7188	0x7188
#define PCI_CHIP_RV515_M64_718A	0x718A
#define PCI_CHIP_RV515_M62_718C	0x718C
#define PCI_CHIP_RV515_M64_718D	0x718D
#define PCI_CHIP_RV516_7193	0x7193
#define PCI_CHIP_RV516_LE_719F	0x719F
#define PCI_CHIP_RV530_71C0	0x71C0
#define PCI_CHIP_RV530_71C2	0x71C2
#define PCI_CHIP_RV530_M56_71C4	0x71C4
#define PCI_CHIP_RV530_M56_71C5	0x71C5
#define PCI_CHIP_RV530_LE_71C6	0x71C6
#define PCI_CHIP_RV535_71C7	0x71C7
#define PCI_CHIP_RV530_LE_71CE	0x71CE
#define PCI_CHIP_RV530_M56_71D4	0x71D4
#define PCI_CHIP_RV530_M66_71D5	0x71D5
#define PCI_CHIP_RV530_M66_71D6	0x71D6
#define PCI_CHIP_RV530_LE_71DE	0x71DE
#define PCI_CHIP_RV535_71E7	0x71E7
#define PCI_CHIP_R580_7240	0x7240
#define PCI_CHIP_R580_7241	0x7241
#define PCI_CHIP_R580_7242	0x7242
#define PCI_CHIP_R580_7243	0x7243
#define PCI_CHIP_R580_7244	0x7244
#define PCI_CHIP_R580_7245	0x7245
#define PCI_CHIP_R580_7246	0x7246
#define PCI_CHIP_R580_7247	0x7247
#define PCI_CHIP_R580_7248	0x7248
#define PCI_CHIP_R580_7249	0x7249
#define PCI_CHIP_R580_724A	0x724A
#define PCI_CHIP_R580_724B	0x724B
#define PCI_CHIP_R580_724C	0x724C
#define PCI_CHIP_R580_724D	0x724D
#define PCI_CHIP_R580_726B	0x726B
#define PCI_CHIP_RV570_7280	0x7280
#define PCI_CHIP_RV570_7288	0x7288
#define PCI_CHIP_RV530_7291	0x7291
#define PCI_CHIP_RV530_7293	0x7293

enum avivo_chip_type {
    CHIP_FAMILY_R520,
    CHIP_FAMILY_R520_M58,
    CHIP_FAMILY_RV515,
    CHIP_FAMILY_RV515_PRO,
    CHIP_FAMILY_RV505,
    CHIP_FAMILY_RV515_M52,
    CHIP_FAMILY_RV515_LE,
    CHIP_FAMILY_RV515_GL,
    CHIP_FAMILY_RV505_CE,
    CHIP_FAMILY_RV516,
    CHIP_FAMILY_RV516_XT,
    CHIP_FAMILY_RV515_M64,
    CHIP_FAMILY_RV515_M62,
    CHIP_FAMILY_RV516_LE,
    CHIP_FAMILY_RV530,
    CHIP_FAMILY_RV530_M56,
    CHIP_FAMILY_RV530_LE,
    CHIP_FAMILY_RV535,
    CHIP_FAMILY_RV530_M66,
    CHIP_FAMILY_R580,
    CHIP_FAMILY_RV570,
    CHIP_FAMILY_LAST,
};

#ifdef PCIACCESS
extern const struct pci_id_match *avivo_device_match;
#endif
extern SymTabRec avivo_chips[];
extern PciChipsets avivo_pci_chips[];

#endif
