#!/bin/sh
./mkdiff.sh $1 $2 | egrep -v '^-' | egrep -v '^\+\+\+' | sed -e 's/^\+\([^	 ]*\)	.*$/^\[^-+]\1.*$/;'
