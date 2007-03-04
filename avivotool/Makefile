all: radeontool

clean:
	rm -f core *~ radeontool

radeontool: radeontool.c radeon_reg.h
	gcc -Wall -Werror -O2 $(shell pkg-config --cflags --libs pciaccess) radeontool.c -o radeontool
