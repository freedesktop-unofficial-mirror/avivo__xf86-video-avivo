#!/bin/sh

REGS=$(grep '#define AVIVO_' ../include/radeon_reg.h | cut -f1 -d'	' | cut -f2 -d' ' | grep -v AVIVO_MC_ | grep -v AVIVO_GPIO_)
for i in $REGS; do grep -q "REGLIST($i)" avivotool.c || echo "        REGLIST($i),"; done
for i in $REGS; do egrep -q "SHOW_REG.*$i" avivotool.c || echo "        SHOW_REG($i);"; done
