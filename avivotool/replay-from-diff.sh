#!/bin/sh
egrep '^\+' $1 | sed -e 's/^\+\([^ 	]*\)	\([^ 	]*\).*$/sudo .\/radeontool regset 0x\1 0x\2/;'
