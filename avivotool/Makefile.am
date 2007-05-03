bin_PROGRAMS = avivotool
avivotool_SOURCES = \
	xf86i2c.c \
	avivotool.c
avivotool_LDADD = \
	$(PCIACCESS_LIBS)

AM_CFLAGS = $(PCIACCESS_CFLAGS)


EXTRA_DIST = \
	check-reglist.sh \
	gen-reg-list.sh \
	mkdiff.sh \
	random-regs.txt \
	replay-from-diff.sh \
	watch-random-regs.sh
