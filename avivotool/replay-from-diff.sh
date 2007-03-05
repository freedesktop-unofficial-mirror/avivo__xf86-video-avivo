#!/bin/sh
egrep '^\+' $1 | sed -e 's/^\+\([^ 	]*\)	\([^ 	]*\).*$/sudo .\/avivotool regset 0x\1 0x\2/;'
