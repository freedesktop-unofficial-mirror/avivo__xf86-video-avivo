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

#define PCI_CHIP_R580_7248             0x7248
#define PCI_CHIP_RV530GL_71D2          0x71D2
#define PCI_CHIP_RV530GL_71DA          0x71DA
#define PCI_CHIP_R520GL_7105           0x7105
#define PCI_CHIP_R520GL_7104           0x7104
#define PCI_CHIP_R520GL_710E           0x710E
#define PCI_CHIP_R520GL_710F           0x710F
#define PCI_CHIP_RV515_719B            0x719B
#define PCI_CHIP_R580_724E             0x724E
#define PCI_CHIP_M56GL_71C4            0x71C4
#define PCI_CHIP_M56GL_71D4            0x71D4
#define PCI_CHIP_M58GL_7106            0x7106
#define PCI_CHIP_M58GL_7103            0x7103
#define PCI_CHIP_M52_714A              0x714A
#define PCI_CHIP_M52_7149              0x7149
#define PCI_CHIP_M52_714B              0x714B
#define PCI_CHIP_M52_714C              0x714C
#define PCI_CHIP_M52_718B              0x718B
#define PCI_CHIP_M52_718C              0x718C
#define PCI_CHIP_M52_7196              0x7196
#define PCI_CHIP_M54_7145              0x7145
#define PCI_CHIP_M54_7186              0x7186
#define PCI_CHIP_M54_718D              0x718D
#define PCI_CHIP_M56_71C5              0x71C5
#define PCI_CHIP_M56_71D5              0x71D5
#define PCI_CHIP_M56_71DE              0x71DE
#define PCI_CHIP_M56_71D6              0x71D6
#define PCI_CHIP_M58_7102              0x7102
#define PCI_CHIP_M58_7101              0x7101
#define PCI_CHIP_M58_7284              0x7284
#define PCI_CHIP_RS690_791E            0x791E
#define PCI_CHIP_RS690M_791F           0x791F
#define PCI_CHIP_R580_7288             0x7288
#define PCI_CHIP_RS600_793F            0x793F
#define PCI_CHIP_RS600_7941            0x7941
#define PCI_CHIP_RS600M_7942           0x7942
#define PCI_CHIP_RV515_7146            0x7146
#define PCI_CHIP_RV515PCI_714E         0x714E
#define PCI_CHIP_RV515_715E            0x715E
#define PCI_CHIP_RV515_714D            0x714D
#define PCI_CHIP_RV535_71C3            0x71C3
#define PCI_CHIP_RV515PCI_718F         0x718F
#define PCI_CHIP_RV515_7142            0x7142
#define PCI_CHIP_RV515_7180            0x7180
#define PCI_CHIP_RV515_7183            0x7183
#define PCI_CHIP_RV515_7187            0x7187
#define PCI_CHIP_RV515_7147            0x7147
#define PCI_CHIP_RV515_715F            0x715F
#define PCI_CHIP_RV515_719F            0x719F
#define PCI_CHIP_RV515_7143            0x7143
#define PCI_CHIP_RV515_7193            0x7193
#define PCI_CHIP_RV530_71CE            0x71CE
#define PCI_CHIP_RV515_7140            0x7140
#define PCI_CHIP_RV530_71C0            0x71C0
#define PCI_CHIP_RV530_71C2            0x71C2
#define PCI_CHIP_RV530_71C6            0x71C6
#define PCI_CHIP_RV515_7181            0x7181
#define PCI_CHIP_RV530_71CD            0x71CD
#define PCI_CHIP_RV535_71C1            0x71C1
#define PCI_CHIP_RV535_7293            0x7293
#define PCI_CHIP_RV535_7291            0x7291
#define PCI_CHIP_RV535_71C7            0x71C7
#define PCI_CHIP_R520_7100             0x7100
#define PCI_CHIP_R520_7108             0x7108
#define PCI_CHIP_R520_7109             0x7109
#define PCI_CHIP_R520_710A             0x710A
#define PCI_CHIP_R520_710B             0x710B
#define PCI_CHIP_R520_710C             0x710C
#define PCI_CHIP_R580_7243             0x7243
#define PCI_CHIP_R580_7245             0x7245
#define PCI_CHIP_R580_7246             0x7246
#define PCI_CHIP_R580_7247             0x7247
#define PCI_CHIP_R580_7248             0x7248
#define PCI_CHIP_R580_7249             0x7249
#define PCI_CHIP_R580_724A             0x724A
#define PCI_CHIP_R580_724B             0x724B
#define PCI_CHIP_R580_724C             0x724C
#define PCI_CHIP_R580_724D             0x724D
#define PCI_CHIP_R580_724F             0x724F
#define PCI_CHIP_R580_7280             0x7280
#define PCI_CHIP_R580_7240             0x7240
#define PCI_CHIP_R580_7244             0x7244

enum avivo_chip_type {
    CHIP_FAMILY_R580,
    CHIP_FAMILY_RV530GL,
    CHIP_FAMILY_R520GL,
    CHIP_FAMILY_RV515,
    CHIP_FAMILY_M56GL,
    CHIP_FAMILY_M58GL,
    CHIP_FAMILY_M52,
    CHIP_FAMILY_M54,
    CHIP_FAMILY_M56,
    CHIP_FAMILY_M58,
    CHIP_FAMILY_RS690,
    CHIP_FAMILY_RS690M,
    CHIP_FAMILY_RS600,
    CHIP_FAMILY_RS600M,
    CHIP_FAMILY_RV515PCI,
    CHIP_FAMILY_RV535,
    CHIP_FAMILY_RV530,
    CHIP_FAMILY_R520,
    CHIP_FAMILY_LAST
};

#ifdef PCIACCESS
extern const struct pci_id_match avivo_device_match[];
#endif
extern SymTabRec avivo_chips[];
extern PciChipsets avivo_pci_chips[];

#endif
