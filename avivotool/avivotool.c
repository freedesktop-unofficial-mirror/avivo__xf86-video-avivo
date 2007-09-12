/* 
 * avivotool v0.1
 * by Daniel Stone <daniel@fooishbar.org>
 *
 * based on:
 * Radeontool   v1.4
 * by Frederick Dean <software@fdd.com>
 *
 * Copyright 2002-2004 Frederick Dean
 * Use hereby granted under the zlib license.
 *
 * Warning: I do not have the Radeon documents, so this was engineered from 
 * the radeon_reg.h header file.  
 *
 * USE RADEONTOOL AT YOUR OWN RISK
 *
 * Thanks to Deepak Chawla, Erno Kuusela, Rolf Offermanns, and Soos Peter
 * for patches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <fnmatch.h>
#include <errno.h>
#include <pciaccess.h>

#include "radeon_reg.h"
#include "xf86i2c.h"

int debug;
int debug_i2c;
int skip;

/* Static: unchanged over the sample period.
 * Partially static: up to three bits changed.
 * Random: Everything changed. */
#define REG_TYPE_STATIC      0
#define REG_TYPE_SEMI_STATIC 1
#define REG_TYPE_RANDOM      2
#define REG_TYPE_UNKNOWN     3
int reg_type[0x8001];

/* *ctrl_mem is mapped to the actual device's memory mapped control area. */
/* Not the address but what it points to is volatile. */
struct pci_device *avivo_device = NULL;
unsigned int ctrl_region, fb_region;
unsigned char * volatile ctrl_mem;
unsigned char * volatile fb_mem;

static void fatal(char *why)
{
    fprintf(stderr, why);
    pci_system_cleanup();
    exit(-1);
}

static void radeon_set(unsigned long offset, const char *name, unsigned int value)
{
    if (debug) 
        printf("writing %s (%lx) -> %08x\n", name, offset, value);

    if (ctrl_mem == NULL)
        fatal("internal error\n");

#ifdef __powerpc__
    __asm__ __volatile__ ("stwbrx %1,%2,%3\n\t"
                          "eieio"
                          : "=m" (*((volatile unsigned int *)ctrl_mem+offset))
                          : "r"(value), "b"(ctrl_mem), "r"(offset));
#else
    *(unsigned int * volatile)(ctrl_mem + offset) = value;  
#endif
}

static void radeon_set_indexed(unsigned long index_offset,
                               unsigned long data_offset, unsigned long offset,
                               const char *name, unsigned int value)
{
    radeon_set(index_offset, "index", offset);
    radeon_set(data_offset, name, value);
}

static unsigned int radeon_get(unsigned long offset, const char *name)
{
    unsigned int value;

    if (debug) 
        printf("reading %s (%lx) is ", name, offset);

    if (ctrl_mem == NULL)
        fatal("internal error\n");

#ifdef __powerpc__
    __asm__ __volatile__ ("lwbrx %0,%1,%2\n\t"
                          "eieio"
                          : "=r" (value)
                          : "b" (ctrl_mem), "r"(offset),
                          "m" (*((volatile unsigned int *)ctrl_mem+offset)));
#else
    value = *(unsigned int * volatile)(ctrl_mem + offset);
#endif

    if (debug) 
        printf("%08x\n", value);

    return value;
}

static unsigned int radeon_get_indexed(unsigned long index_offset,
                                       unsigned long data_offset,
                                       unsigned long offset, const char *name)
{
    radeon_set(index_offset, "index", offset);
    return radeon_get(data_offset, name);
}

static unsigned int radeon_get_mc(unsigned long offset, const char *name)
{
    return radeon_get_indexed(AVIVO_MC_INDEX, AVIVO_MC_DATA,
                              offset | 0x007f0000, name);
}

static void radeon_set_mc(unsigned long offset, const char *name,
                          unsigned int value)
{
    return radeon_set_indexed(AVIVO_MC_INDEX, AVIVO_MC_DATA,
                              offset | 0x00ff0000, name, value);
}

static void usage(void)
{
    printf("usage: avivotool [options] [command]\n");
    printf("         --debug            - show a little debug info\n");
    printf("         --skip=1           - use the second radeon card\n");
    printf("         regs <set>         - show a listing of some random registers\n");
    printf("                              <set> restricts: all, core, mc, crtc1, cur1\n");
    printf("         regmatch <pattern> - show registers matching wildcard pattern\n");
    printf("         regset <pattern> <value> - set registers matching wildcard pattern\n");
    printf("         romtables <path>   - dumps the BIOS tables from either a given path\n");
    printf("                              or 'mmap' to get it from memory\n");
    printf("         output <output> <enable|disable> - turns the specified\n");
    printf("                                            output on or off\n");
    printf("         i2c-monitor        - do something\n");
    printf("         i2c-monitor <gpio_in> <gpio_out> - do something\n");
    exit(-1);
}

#define GET_REG(r) radeon_get(r, #r)
#define SET_REG(r, v) radeon_set(r, #r, v)

void _i2c_set(unsigned long offset, unsigned int value)
{
	if (debug_i2c)
		printf("OUTREG(0x%08lX, 0x%08X);\n", offset, value);
	SET_REG(offset, value);
}

unsigned int _i2c_get(unsigned long offset)
{
	unsigned int value;

	value = GET_REG(offset);
	if (debug_i2c)
		printf("tmp = INREG(0x%08lX);\t/* should get 0x%08X */\n",
		       offset, value);
	return value;
}

void _i2c_stop(void)
{
	_i2c_set(AVIVO_I2C_STATUS,
		 AVIVO_I2C_STATUS_DONE | AVIVO_I2C_STATUS_NACK | AVIVO_I2C_STATUS_HALT);
	usleep(1000);
	_i2c_set(AVIVO_I2C_STOP, 1);
	usleep(1000);
	_i2c_set(AVIVO_I2C_STOP, 0);
	usleep(1000);
	_i2c_set(AVIVO_I2C_START_CNTL, 0);
}

void _i2c_wait(void)
{
	int i, num_ready;
	unsigned int tmp;

	_i2c_set(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_CMD_WAIT);
	for (i = 0, num_ready = 0; num_ready < 3; i++) {
		tmp = _i2c_get(AVIVO_I2C_STATUS);
		if (tmp == AVIVO_I2C_STATUS_DONE) {
			num_ready++;
		} else if (tmp != AVIVO_I2C_STATUS_CMD_WAIT) {
			_i2c_stop();
		}
		/* Timeout. */
		if (i == 10) {
			fprintf(stderr, "i2c timeout\n");
			exit(1);
		}
		usleep(1000);
	}
	_i2c_set(AVIVO_I2C_STATUS, AVIVO_I2C_STATUS_DONE);	
}

void _i2c_start(unsigned int connector)
{
	unsigned int tmp;
	
	tmp = _i2c_get(AVIVO_I2C_CNTL);
	if (tmp != 1) {
		_i2c_set(AVIVO_I2C_CNTL, AVIVO_I2C_EN);
		_i2c_stop();
		_i2c_set(AVIVO_I2C_START_CNTL, (AVIVO_I2C_START | connector));
		tmp = _i2c_get(AVIVO_I2C_7D3C) & (~0xff) & (~AVIVO_I2C_7D3C_SIZE_MASK);
		_i2c_set(AVIVO_I2C_7D3C, tmp | 1);
	}
	tmp = _i2c_get(AVIVO_I2C_START_CNTL);
	_i2c_set(AVIVO_I2C_START_CNTL, tmp | AVIVO_I2C_START);
}

static void
_i2c_read(unsigned char *buf, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        *buf = _i2c_get(AVIVO_I2C_DATA) & 0xff;
        buf++;
	usleep(1000);
    }
}

static void
_i2c_write(unsigned char *buf, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        _i2c_set(AVIVO_I2C_DATA, *buf);
        buf++;
	usleep(1000);
    }
}


void _i2c_write_read(unsigned char *write_buf, int num_write,
                     unsigned char *read_buf, int num_read,
                     unsigned int connector)
{
	unsigned int tmp;

	if (num_write) {
		_i2c_start(connector);
		tmp = _i2c_get(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
		tmp |= num_write << AVIVO_I2C_7D3C_SIZE_SHIFT; 
		_i2c_set(AVIVO_I2C_7D3C, tmp);
		tmp = _i2c_get(AVIVO_I2C_7D40);
		_i2c_set(AVIVO_I2C_7D40, tmp);

		_i2c_set(AVIVO_I2C_DATA, 0xA0);

		_i2c_write(write_buf, num_write);
		tmp = _i2c_get(AVIVO_I2C_START_CNTL) & (~AVIVO_I2C_STATUS_MASK);
		_i2c_set(AVIVO_I2C_START_CNTL,
                 tmp
                 | AVIVO_I2C_STATUS_DONE
                 | AVIVO_I2C_STATUS_NACK);
		_i2c_wait();
	}

	if (num_read) {
		_i2c_set(AVIVO_I2C_DATA, 0xA0 | 1);
		tmp = _i2c_get(AVIVO_I2C_7D3C) & (~AVIVO_I2C_7D3C_SIZE_MASK);
		tmp |= num_read << AVIVO_I2C_7D3C_SIZE_SHIFT; 
		_i2c_set(AVIVO_I2C_7D3C, tmp);
		tmp = _i2c_get(AVIVO_I2C_START_CNTL) & (~AVIVO_I2C_STATUS_MASK);
		_i2c_set(AVIVO_I2C_START_CNTL,
                 tmp
                 | AVIVO_I2C_STATUS_DONE
                 | AVIVO_I2C_STATUS_NACK
                 | AVIVO_I2C_STATUS_HALT);
		_i2c_wait();
		_i2c_read(read_buf, num_read);
		_i2c_stop();
	}	
}

void radeon_i2c(void)
{
	int i, j;
	unsigned char wbuf[128];
	unsigned char rbuf[128];
	unsigned int rsize = 15;

	for (i = 0; i < 1; i++) {
		wbuf[0] = i * 4;
		_i2c_write_read(wbuf, 1, rbuf, rsize, AVIVO_I2C_CONNECTOR1);
		for (j = 0; j < rsize; j++) {
			printf("%02X", rbuf[j]);
		}
	}
	printf("\n");
}

#define DEFAULT_GPIO_IN  0x7E5C
#define DEFAULT_GPIO_OUT 0x7E58

static int GPIO_IN;
static int GPIO_OUT;

static void AVIVOI2CGetBits(I2CBusPtr b, int *Clock, int *data)
{
    unsigned long  val;

    /* Get the result */
    val = GET_REG(GPIO_IN);
    if (GPIO_IN != 0x7E3C) {
        *Clock = (val & (1<<0)) != 0;
        *data  = (val & (1<<8)) != 0;
    } else {
        *Clock = (val & (1<<19)) != 0;
        *data  = (val & (1<<18)) != 0;
    }
    if (debug_i2c)
        printf("IN 0x%08lX -> clock = %d, data = %d\n", val, *Clock, *data);
}

static void AVIVOI2CPutBits(I2CBusPtr b, int Clock, int data)
{
    unsigned long  val;

    val = 0;
    if (GPIO_IN != 0x7E3C) {
        val |= (Clock ? 0:(1<<0));
        val |= (data ? 0:(1<<8));
    } else {
        val |= (Clock ? 0:(1<<19));
        val |= (data ? 0:(1<<18));
    }
    if (debug_i2c)
        printf("OUT 0x%08lX (Clock = %d, data = %d)\n", val, Clock, data);
    SET_REG(GPIO_OUT, val);
    /* read back to improve reliability on some cards. */
    val = GET_REG(GPIO_OUT);
}

void i2c_initialize(void)
{
    int tmp;

    tmp = GET_REG(GPIO_OUT - 0x8);
    SET_REG(GPIO_OUT - 0x8, tmp);
    tmp = GET_REG(GPIO_OUT - 0x8) | 0x101;
    SET_REG(GPIO_OUT - 0x8, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0x100);
    SET_REG(GPIO_OUT, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0x101);
    SET_REG(GPIO_OUT, 0x0);
    tmp = GET_REG(GPIO_OUT - 0x4) & (~0x100);
    SET_REG(GPIO_OUT - 0x4, tmp);
    tmp = GET_REG(GPIO_OUT - 0x4) & (~0x101);
    SET_REG(GPIO_OUT - 0x4, 0x0);
    tmp = GET_REG(GPIO_OUT) & (~0x100);
    SET_REG(GPIO_OUT, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0x101);
    SET_REG(GPIO_OUT, 0x0);
}

void i2c_initialize_7e30(void)
{
    int tmp;

    tmp = GET_REG(GPIO_OUT - 0x8);
    SET_REG(GPIO_OUT - 0x8, tmp);
    tmp = GET_REG(GPIO_OUT - 0x8) | 0xC0000;
    SET_REG(GPIO_OUT - 0x8, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0x80000);
    SET_REG(GPIO_OUT, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0xC0000);
    SET_REG(GPIO_OUT, 0x0);
    tmp = GET_REG(GPIO_OUT - 0x4) & (~0x80000);
    SET_REG(GPIO_OUT - 0x4, tmp);
    tmp = GET_REG(GPIO_OUT - 0x4) & (~0xC0000);
    SET_REG(GPIO_OUT - 0x4, 0x0);
    tmp = GET_REG(GPIO_OUT) & (~0x80000);
    SET_REG(GPIO_OUT, tmp);
    tmp = GET_REG(GPIO_OUT) & (~0xC0000);
    SET_REG(GPIO_OUT, 0x0);
}

void radeon_i2c_monitor(int gpio_in, int gpio_out)
{
    I2CBusPtr i2cbus;
	I2CByte wbuf[128];
	I2CByte rbuf[128];
    int i, j;
    I2CDevPtr dev;

    GPIO_IN = gpio_in;
    GPIO_OUT = gpio_out;

    printf("GPIO_IN = 0x%X, GPIO_OUT = 0x%X\n", GPIO_IN, GPIO_OUT);
    if (GPIO_IN != 0x7E3C) {
        i2c_initialize();
    } else {
        i2c_initialize_7e30();
    }
    i2cbus = xf86CreateI2CBusRec();
    if (!i2cbus) {
        return;
    }

    i2cbus->BusName    = "DDC";
    i2cbus->I2CPutBits = AVIVOI2CPutBits;
    i2cbus->I2CGetBits = AVIVOI2CGetBits;
    i2cbus->AcknTimeout = 5;

    if (!xf86I2CBusInit(i2cbus))
        return;

    if (!(dev = xf86I2CFindDev(i2cbus, 0x00A0))) {
        dev = xf86CreateI2CDevRec();
        dev->DevName = "ddc2";
        dev->SlaveAddr = 0xA0;
        dev->ByteTimeout = 2200; /* VESA DDC spec 3 p. 43 (+10 %) */
        dev->StartTimeout = 550;
        dev->BitTimeout = 40;
        dev->AcknTimeout = 40;

        dev->pI2CBus = i2cbus;
        if (!xf86I2CDevInit(dev)) {
            fprintf(stderr, "No DDC2 device\n");
            return;
        }
    } else {
        fprintf(stderr, "No device at 0xA0\n");
    }

    wbuf[0] = 0x0;
    xf86I2CWriteRead(dev, wbuf, 1, rbuf, 128);
    for (j=0; j < 8; j++) {
        for (i=0; i < 16; i++) {
            printf("%02X", rbuf[i + j * 16]);
        }
        printf("\n");
    }
    printf("\n");
}

void radeon_i2c_monitor_default(void)
{
    radeon_i2c_monitor(DEFAULT_GPIO_IN, DEFAULT_GPIO_OUT);
}

void radeon_output_set(char *output, char *status)
{
    int on = (strncmp(status, "en", 2) == 0);

    if (strcmp(output, "tmds1") == 0) {
        if (on) {
            SET_REG(AVIVO_TMDSA_TRANSMITTER_CONTROL, 0x10000011);
            SET_REG(AVIVO_TMDSA_TRANSMITTER_ENABLE, 0x0000001f);
            SET_REG(AVIVO_TMDSA_CNTL, 0x00001010 | AVIVO_TMDSA_CNTL_ENABLE);
        }
        else {
            SET_REG(AVIVO_TMDSA_CNTL, 0x00001010);
            SET_REG(AVIVO_TMDSA_BIT_DEPTH_CONTROL, 0x04000000);
            SET_REG(AVIVO_TMDSA_DATA_SYNCHRONIZATION, 0x00000000);
            SET_REG(AVIVO_TMDSA_TRANSMITTER_CONTROL, 0x10000011);
            SET_REG(AVIVO_TMDSA_TRANSMITTER_ENABLE, 0x00060000);
        }
    }
    else if (strcmp(output, "tmds2") == 0) {
        if (on) {
            SET_REG(AVIVO_LVTMA_TRANSMITTER_CONTROL, 0x30000011);
            SET_REG(AVIVO_LVTMA_TRANSMITTER_ENABLE, 0x0000003e);
            SET_REG(AVIVO_LVTMA_CNTL, 0x00001010 | AVIVO_TMDSA_CNTL_ENABLE);
        }
        else {
            SET_REG(AVIVO_LVTMA_CNTL, 0x1010);
            SET_REG(AVIVO_LVTMA_BIT_DEPTH_CONTROL, 0x04000000);
            SET_REG(AVIVO_LVTMA_DATA_SYNCHRONIZATION, 0x00000000);
            SET_REG(AVIVO_LVTMA_TRANSMITTER_CONTROL, 0x10000011);
            SET_REG(AVIVO_LVTMA_TRANSMITTER_ENABLE, 0x00060000);
        }
    }
    else if (strcmp(output, "dac1") == 0) {
        if (on) {
            SET_REG(AVIVO_DAC1_MYSTERY1, 0x00000000);
            SET_REG(AVIVO_DAC1_MYSTERY2, 0x00000000);
            SET_REG(AVIVO_DAC1_CNTL, AVIVO_DAC_EN);
        }
        else {
            SET_REG(AVIVO_DAC1_CNTL, 0x00000000);
            SET_REG(AVIVO_DAC1_MYSTERY1, AVIVO_DAC_MYSTERY1_DIS);
            SET_REG(AVIVO_DAC1_MYSTERY2, AVIVO_DAC_MYSTERY2_DIS);
        }
    }
    else if (strcmp(output, "dac2") == 0) {
        if (on) {
            SET_REG(AVIVO_DAC2_MYSTERY1, 0x00000000);
            SET_REG(AVIVO_DAC2_MYSTERY2, 0x00000000);
            SET_REG(AVIVO_DAC2_CNTL, AVIVO_DAC_EN);
        }
        else {
            SET_REG(AVIVO_DAC2_CNTL, 0x00000000);
            SET_REG(AVIVO_DAC2_MYSTERY1, AVIVO_DAC_MYSTERY1_DIS);
            SET_REG(AVIVO_DAC2_MYSTERY2, AVIVO_DAC_MYSTERY2_DIS);
        }
    }
    else if (strcmp(output, "crtc1") == 0) {
        if (on) {
            /* Select graphics mode? */
            SET_REG(0x00000330, 0x00010600);
            SET_REG(0x00000338, 0x00000400);
            SET_REG(AVIVO_CRTC1_FB_LOCATION,
                    avivo_device->regions[fb_region].base_addr);
            /* These modelines are all hardcoded for my 1280x1024;
             * adjust to suit. */
            SET_REG(AVIVO_CRTC1_FB_END,
                    avivo_device->regions[fb_region].base_addr +
                     (1280 * 1024 * 4)); 
            SET_REG(AVIVO_CRTC1_X_LENGTH, 1280);
            SET_REG(AVIVO_CRTC1_Y_LENGTH, 1024);
            SET_REG(AVIVO_CRTC1_PITCH, 1280);
            SET_REG(AVIVO_CRTC1_H_TOTAL, 1687);
            SET_REG(AVIVO_CRTC1_H_BLANK, 1672 | (392 << 16));
            SET_REG(AVIVO_CRTC1_H_SYNC_WID, (144 << 16));
            SET_REG(AVIVO_CRTC1_H_SYNC_POL, 0x00000000);
            SET_REG(AVIVO_CRTC1_V_TOTAL, 1065);
            SET_REG(AVIVO_CRTC1_V_BLANK, 1065 | (41 << 16));
            SET_REG(AVIVO_CRTC1_V_SYNC_WID, (3 << 16));
            SET_REG(AVIVO_CRTC1_V_SYNC_POL, 0x00000000);
            SET_REG(AVIVO_CRTC1_FB_FORMAT, AVIVO_CRTC_FORMAT_ARGB32);
            SET_REG(AVIVO_CRTC1_MODE, 0x00000000);
            SET_REG(AVIVO_CRTC1_CNTL, AVIVO_CRTC_EN);
            SET_REG(AVIVO_CRTC1_SCAN_ENABLE, AVIVO_CRTC_SCAN_EN);
        }
        else {
            SET_REG(AVIVO_CRTC1_SCAN_ENABLE, 0x00000000);
            SET_REG(AVIVO_CRTC1_CNTL, 0x00000000);
        }
    }
    else if (strcmp(output, "lvds") == 0) {
        if (on)
            SET_REG(AVIVO_LVDS_CNTL, GET_REG(AVIVO_LVDS_CNTL) | AVIVO_LVDS_EN);
        else
            SET_REG(AVIVO_LVDS_CNTL, GET_REG(AVIVO_LVDS_CNTL) &
                                      ~(AVIVO_LVDS_EN));
    }
    else if (strcmp(output, "cur1") == 0) {
        if (on) {
            SET_REG(AVIVO_CURSOR1_LOCATION, GET_REG(AVIVO_CRTC1_FB_END));
            SET_REG(AVIVO_CURSOR1_SIZE, (32 << 16) | 32);
            SET_REG(AVIVO_CURSOR1_CNTL, AVIVO_CURSOR_EN |
                                        (AVIVO_CURSOR_FORMAT_ARGB <<
                                         AVIVO_CURSOR_FORMAT_SHIFT));
        }
        else {
            SET_REG(AVIVO_CURSOR1_CNTL, 0x00000000);
        }
    }
    else {
        fatal("unknown output\n");
    }
}

int radeon_get_fb_params(char *crtc, int write, unsigned long *location, int *len)
{
    unsigned long format;
    
    if (strcmp(crtc, "crtc1") == 0 || strcmp(crtc, "1") == 0) {
        *location = GET_REG(AVIVO_CRTC1_FB_LOCATION);
        *len = GET_REG(AVIVO_CRTC1_FB_END) - *location;
    }
    else if (strcmp(crtc, "crtc2") == 0 || strcmp(crtc, "2") == 0) {
        *location = GET_REG(AVIVO_CRTC2_FB_LOCATION);
        *len = GET_REG(AVIVO_CRTC2_FB_END) - *location;
    }
    else if (strcmp(crtc, "cur1") == 0) {
        *location = GET_REG(AVIVO_CURSOR1_LOCATION);
        *len = 64 * 64;
        format = (GET_REG(AVIVO_CURSOR1_CNTL) & AVIVO_CURSOR_FORMAT_MASK) >>
                 AVIVO_CURSOR_FORMAT_SHIFT;

        if (format == AVIVO_CURSOR_FORMAT_ARGB ||
            format == AVIVO_CURSOR_FORMAT_ABGR) {
            *len *= 4;
        }
        else {
            return 0;
        }
    }
    else {
        return 0;
    }

    *location -= avivo_device->regions[fb_region].base_addr;
    *location += (unsigned long) fb_mem;

    return 1;
}

void radeon_dump_img(char *type)
{
    int len, i, ret;
    unsigned long location;
    unsigned char * volatile fb;
    
    if (!radeon_get_fb_params(type, 0, &location, &len) || !len)
        fatal("mapping location to dump\n");

    fb = (unsigned char * volatile) location;

    i = 0;
    while (i < len) {
        ret = write(STDOUT_FILENO, &(fb[i]), len - i);
        if (ret < 0) {
            fprintf(stderr, "write died: %s\n", strerror(errno));
            fatal("writing to stdout\n");
        }
        i += ret;
    }

    return;
}

void radeon_load_img(char *type)
{
    int len, i, ret;
    unsigned long location;
    unsigned char * volatile fb;
    
    if (!radeon_get_fb_params(type, 1, &location, &len) || !len)
        fatal("mapping framebuffer to load\n");

    fb = (unsigned char * volatile) location;

    i = 0;
    while (i < len) {
        ret = read(STDIN_FILENO, &(fb[i]), len - i);
        if (ret < 0) {
            fprintf(stderr, "read died: %s\n", strerror(errno));
            fatal("reading from stdin\n");
        }
        i += ret;
    }

    return;
}

int get_mask(int num_bits)
{
    int i, ret = 0;

    for (i = 0; i < num_bits; i++)
        ret |= (1 << i);

    return ret;
}

void __attribute__((__sentinel__(0)))
radeon_show_reg_bits(const char *name, unsigned long index_addr,
                     unsigned long data_addr, unsigned long addr, ...)
{
    va_list ap;
    int start, end;
    char *desc;
    int value;
    char format[32], intformat;

    if (index_addr && data_addr)
        value = radeon_get_indexed(index_addr, data_addr, addr, name);
    else
        value = radeon_get(addr, name);

    printf("%s\t%08x\n", name, value);

    va_start(ap, addr);
    while (1) {
        start = va_arg(ap, int);
        end = va_arg(ap, int);
        desc = va_arg(ap, char *);

        if (!start && !end && !desc)
            break;

        if (strncmp(desc, "DECIMAL", 7) == 0) {
            desc += 7;
            intformat = 'd';
        }
        else {
            intformat = 'x';
        }
        /* FIXME There has to be a better way ... */
        sprintf(format, "\t%%s:\t%%%d%c\n", end - start + 1, intformat);
        printf(format, desc, (value >> start) & ((1 << (end - start + 1)) - 1));
    }
    va_end(ap);
}

#define REGLIST(r) { "", #r, radeon_get, radeon_set, r }
#define REGLIST_MC(r) { "MC: ", #r, radeon_get_mc, radeon_set_mc, r }
static struct {
    const char *type;
    const char *name;
    unsigned int (*get)(unsigned long, const char *);
    void (*set)(unsigned long, const char *, unsigned int);
    unsigned address;
} reg_list[] = {
    REGLIST_MC(MC00),
    REGLIST_MC(MC01),
    REGLIST_MC(MC02),
    REGLIST_MC(MC03),
    REGLIST_MC(AVIVO_MC_MEMORY_MAP),
    REGLIST_MC(MC05),
    REGLIST_MC(MC06),
    REGLIST_MC(MC07),
    REGLIST_MC(MC08),
    REGLIST_MC(MC09),
    REGLIST_MC(MC0a),
    REGLIST_MC(MC0b),
    REGLIST_MC(MC0c),
    REGLIST_MC(MC0d),
    REGLIST_MC(MC0e),
    REGLIST_MC(MC0f),
    REGLIST_MC(MC10),
    REGLIST_MC(MC11),
    REGLIST_MC(MC12),
    REGLIST_MC(MC13),
    REGLIST_MC(MC14),
    REGLIST_MC(MC15),
    REGLIST_MC(MC16),
    REGLIST_MC(MC17),
    REGLIST_MC(MC18),
    REGLIST_MC(MC19),
    REGLIST_MC(MC1a),
    REGLIST_MC(MC1b),
    REGLIST_MC(MC1c),
    REGLIST_MC(MC1d),
    REGLIST_MC(MC1e),
    REGLIST_MC(MC1f),
    REGLIST(AVIVO_ENGINE_STATUS),
    REGLIST(AVIVO_CRTC1_H_TOTAL),
    REGLIST(AVIVO_CRTC1_H_BLANK),
    REGLIST(AVIVO_CRTC1_H_SYNC_WID),
    REGLIST(AVIVO_CRTC1_H_SYNC_POL),
    REGLIST(AVIVO_CRTC1_V_TOTAL),
    REGLIST(AVIVO_CRTC1_V_BLANK),
    REGLIST(AVIVO_CRTC1_V_SYNC_WID),
    REGLIST(AVIVO_CRTC1_V_SYNC_POL),
    REGLIST(AVIVO_CRTC1_CNTL),
    REGLIST(AVIVO_CRTC1_MODE),
    REGLIST(AVIVO_CRTC1_SCAN_ENABLE),
    REGLIST(AVIVO_CRTC1_FB_FORMAT),
    REGLIST(AVIVO_CRTC1_FB_LOCATION),
    REGLIST(AVIVO_CRTC1_FB_END),
    REGLIST(AVIVO_CRTC1_PITCH),
    REGLIST(AVIVO_CRTC1_X_LENGTH),
    REGLIST(AVIVO_CRTC1_Y_LENGTH),
    REGLIST(AVIVO_CRTC1_OFFSET_START),
    REGLIST(AVIVO_CRTC1_OFFSET_END),
    REGLIST(AVIVO_CRTC1_EXPANSION_CNTL),
    REGLIST(AVIVO_CRTC1_EXPANSION_SOURCE),
    REGLIST(AVIVO_CRTC2_H_TOTAL),
    REGLIST(AVIVO_CRTC2_H_BLANK),
    REGLIST(AVIVO_CRTC2_H_SYNC_WID),
    REGLIST(AVIVO_CRTC2_H_SYNC_POL),
    REGLIST(AVIVO_CRTC2_V_TOTAL),
    REGLIST(AVIVO_CRTC2_V_BLANK),
    REGLIST(AVIVO_CRTC2_V_SYNC_WID),
    REGLIST(AVIVO_CRTC2_V_SYNC_POL),
    REGLIST(AVIVO_CRTC2_CNTL),
    REGLIST(AVIVO_CRTC2_MODE),
    REGLIST(AVIVO_CRTC2_SCAN_ENABLE),
    REGLIST(AVIVO_CRTC2_FB_FORMAT),
    REGLIST(AVIVO_CRTC2_FB_LOCATION),
    REGLIST(AVIVO_CRTC2_FB_END),
    REGLIST(AVIVO_CRTC2_PITCH),
    REGLIST(AVIVO_CRTC2_X_LENGTH),
    REGLIST(AVIVO_CRTC2_Y_LENGTH),
    REGLIST(AVIVO_DAC1_CNTL),
    REGLIST(AVIVO_DAC1_MYSTERY1),
    REGLIST(AVIVO_DAC1_MYSTERY2),
    REGLIST(AVIVO_DAC2_CNTL),
    REGLIST(AVIVO_DAC2_MYSTERY1),
    REGLIST(AVIVO_DAC2_MYSTERY2),
    REGLIST(AVIVO_TMDSA_CNTL),
    REGLIST(AVIVO_TMDSA_TRANSMITTER_ENABLE),
    REGLIST(AVIVO_TMDSA_BIT_DEPTH_CONTROL),
    REGLIST(AVIVO_TMDSA_DATA_SYNCHRONIZATION),
    REGLIST(AVIVO_TMDSA_TRANSMITTER_CONTROL),
    REGLIST(AVIVO_LVTMA_CNTL),
    REGLIST(AVIVO_LVTMA_CLOCK_ENABLE),
    REGLIST(AVIVO_LVTMA_TRANSMITTER_ENABLE),
    REGLIST(AVIVO_LVTMA_BIT_DEPTH_CONTROL),
    REGLIST(AVIVO_LVTMA_DATA_SYNCHRONIZATION),
    REGLIST(AVIVO_LVTMA_TRANSMITTER_CONTROL),
    REGLIST(AVIVO_TMDS_STATUS),
    REGLIST(AVIVO_LVDS_CNTL),
    REGLIST(AVIVO_LVDS_BACKLIGHT_CNTL),
    REGLIST(AVIVO_CURSOR1_CNTL),
    REGLIST(AVIVO_CURSOR1_POSITION),
    REGLIST(AVIVO_CURSOR1_LOCATION),
    REGLIST(AVIVO_CURSOR1_SIZE),
};

/* If you want to be _really_ sure, try something like 20, with a
 * 100000us delay.  But that'll take a while. */
#define REG_NUM_SAMPLES 10
#define REG_SLEEP 50000

int get_reg_type(unsigned long address)
{
    int value, prev, bits = 0;
    int i, j;

    for (i = 0; i < REG_NUM_SAMPLES; i++) {
        value = radeon_get(address, "static sampling");
        if (i > 0 && (prev ^ value))
            bits |= (prev ^ value);
        usleep(REG_SLEEP);
        prev = value;
    }

    j = 0;
    for (i = 0; i < 32; i++) {
        if (bits & (1 << i))
            j++;
    }

    if (j == 0)
        i = REG_TYPE_STATIC;
    else if (j < 3)
        i = REG_TYPE_SEMI_STATIC;
    else
        i = REG_TYPE_RANDOM;

    reg_type[address] = i;

    return i;
}

const char *get_reg_name(unsigned long address, const char *type)
{
    int i = 0;

    for (i = 0; i < sizeof(reg_list) / sizeof(reg_list[0]); i++) {
        if (reg_list[i].address == address &&
            (strcmp(type, reg_list[i].type) == 0))
            return reg_list[i].name;
    }

    return NULL;
}

void radeon_cmd_regs(const char *type)
{
    #define GET_REG(r) radeon_get(r, #r)
    #define SHOW_REG(r) printf("%s\t%08x\n", #r, radeon_get(r, #r))
    #define SHOW_UNKNOWN_REG(r) { tmp = radeon_get(r, "byhand"); printf("%08lx\t%08x (%d)\n", r, tmp, tmp); }
    #define REG_TYPE_NAME(r) ((r == REG_TYPE_STATIC) ? "static" : ((r == REG_TYPE_SEMI_STATIC) ? "semi-static" : "random"))
    #define SHOW_STATIC_REG(r) { tmp = get_reg_type(r); printf("%s (%08lx)\t%s\n", get_reg_name(r, ""), r, REG_TYPE_NAME(tmp)); }
    #define SHOW_REG_DECIMAL(r) printf("%s\t%d (decimal)\n", #r, radeon_get(r, #r))
    #define SHOW_REG_BITS(r, ...) radeon_show_reg_bits(#r, 0, 0, r, __VA_ARGS__)
    #define SHOW_MC_REG(r) printf("%s\t%08x\n", #r, radeon_get_indexed(AVIVO_MC_INDEX, AVIVO_MC_DATA, (r | 0x007f0000), #r))
    #define SHOW_MC_REG_BITS(r, ...) radeon_show_reg_bits(AVIVO_MC_INDEX, AVIVO_MC_DATA, #r, (r | 0x007f0000), __VA_ARGS__)

    int show_all = (strcmp(type, "all") == 0);
    int show_core = (show_all || strstr(type, "core"));
    int show_mc = (show_all || strstr(type, "mc"));
    int show_crtc1 = (show_all || strstr(type, "crtc1"));
    int show_crtc2 = (show_all || strstr(type, "crtc2"));
    int show_dac1 = (show_all || strstr(type, "dac1"));
    int show_dac2 = (show_all || strstr(type, "dac2"));
    int show_tmds1 = (show_all || strstr(type, "tmds1"));
    int show_tmds2 = (show_all || strstr(type, "tmds2"));
    int show_lvds = (show_all || strstr(type, "lvds"));
    int show_cur1 = (show_all || strstr(type, "cur1"));
    int shut_up = 1;
    int tmp; /* may be stomped at any moment. */
    unsigned long i;

    if (strcmp(type, "default") == 0) {
        shut_up = 0;
        show_core = 1;
        show_mc = 1;
        if (GET_REG(AVIVO_CRTC1_CNTL) & AVIVO_CRTC_EN)
            show_crtc1 = 1;
        if (GET_REG(AVIVO_CRTC2_CNTL) & AVIVO_CRTC_EN)
            show_crtc2 = 1;
        if (GET_REG(AVIVO_DAC1_CNTL) & AVIVO_DAC_EN)
            show_dac1 = 1;
        if (GET_REG(AVIVO_DAC2_CNTL) & AVIVO_DAC_EN)
            show_dac2 = 1;
        if (GET_REG(AVIVO_TMDSA_CNTL) & AVIVO_TMDSA_CNTL_ENABLE)
            show_tmds1 = 1;
        if (GET_REG(AVIVO_LVTMA_CNTL) & AVIVO_TMDSA_CNTL_ENABLE)
            show_tmds2 = 1;
        if (GET_REG(AVIVO_LVDS_EN) & AVIVO_LVDS_EN)
            show_lvds = 1;
        if (GET_REG(AVIVO_CURSOR1_CNTL) & AVIVO_CURSOR_EN)
            show_cur1 = 1;
    }

    if (strcmp(type, "dynamic") == 0) {
        printf("Starting static/dynamic analysis; this will take a while ...\n");
        memset(reg_type, REG_TYPE_UNKNOWN, sizeof(reg_type) / sizeof(reg_type[0]));

        for (i = 0x0000; i < AVIVO_ENGINE_STATUS; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_ENGINE_STATUS + 0x4; i < AVIVO_MC_INDEX; i += 4)
            SHOW_STATIC_REG(i);
        /* Attempting to read 40c8 will bring your machine down hard. */
        for (i = AVIVO_MC_DATA + 0x4; i < 0x40c8; i += 4)
            SHOW_STATIC_REG(i);
        /* Ditto 4ff8 and 4ffc.  They may be indexing registers, or they
         * may just kill your system. */
        for (i = 0x40cc; i < 0x4ff8; i += 4)
            SHOW_STATIC_REG(i);
        for (i = 0x5000; i < AVIVO_CRTC1_H_TOTAL; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CRTC1_V_SYNC_POL + 0x4; i < AVIVO_CRTC1_CNTL; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CRTC1_CNTL + 0x4; i < AVIVO_CRTC1_SCAN_ENABLE; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CRTC1_Y_LENGTH + 0x4; i < AVIVO_CURSOR1_CNTL; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CURSOR1_POSITION + 0x4; i < AVIVO_CRTC2_H_TOTAL; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CRTC2_V_SYNC_POL + 0x4; i < AVIVO_CRTC2_CNTL; i += 4)
            SHOW_STATIC_REG(i);
        for (i = AVIVO_CRTC2_CNTL + 0x4; i < AVIVO_CRTC2_SCAN_ENABLE; i += 4)
            SHOW_STATIC_REG(i);
        /* Reading to the end of the range is also harmful. */
        for (i = AVIVO_CRTC2_Y_LENGTH + 0x4; i < 0x7ff8; i += 4)
            SHOW_STATIC_REG(i);

        return;
    }

    /* Dump all as-yet-unknown registers. */
    if (strcmp(type, "unknown") == 0) {
        for (i = 0x0000; i < AVIVO_ENGINE_STATUS; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_ENGINE_STATUS + 0x4; i < AVIVO_MC_INDEX; i += 4)
            SHOW_UNKNOWN_REG(i);
        /* Attempting to read 40c8 will bring your machine down hard. */
        for (i = AVIVO_MC_DATA + 0x4; i < 0x40c8; i += 4)
            SHOW_UNKNOWN_REG(i);
        /* Ditto 4ff8 and 4ffc.  They may be indexing registers, or they
         * may just kill your system. */
        for (i = 0x40cc; i < 0x4ff8; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = 0x5000; i < AVIVO_CRTC1_H_TOTAL; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CRTC1_V_SYNC_POL + 0x4; i < AVIVO_CRTC1_CNTL; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CRTC1_CNTL + 0x4; i < AVIVO_CRTC1_SCAN_ENABLE; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CRTC1_Y_LENGTH + 0x4; i < AVIVO_CURSOR1_CNTL; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CURSOR1_POSITION + 0x4; i < AVIVO_CRTC2_H_TOTAL; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CRTC2_V_SYNC_POL + 0x4; i < AVIVO_CRTC2_CNTL; i += 4)
            SHOW_UNKNOWN_REG(i);
        for (i = AVIVO_CRTC2_CNTL + 0x4; i < AVIVO_CRTC2_SCAN_ENABLE; i += 4)
            SHOW_UNKNOWN_REG(i);
        /* Reading to the end of the range is also harmful. */
        for (i = AVIVO_CRTC2_Y_LENGTH + 0x4; i < 0x7ff8; i += 4)
            SHOW_UNKNOWN_REG(i);

        return;
    }

    /* Dump all registers that we can read. */
    if (strcmp(type, "all") == 0) {
        /* Attempting to read 40c8 will bring your machine down hard. */
        for (i = 0x0000; i < 0x40c8; i += 4)
            SHOW_UNKNOWN_REG(i);
        /* Ditto 4ff8 and 4ffc.  They may be indexing registers, or they
         * may just kill your system. */
        for (i = 0x40cc; i < 0x4ff8; i += 4)
            SHOW_UNKNOWN_REG(i);
        /* Reading to the end of the range is also harmful. */
        for (i = 0x5000; i < 0x7ff8; i += 4)
            SHOW_UNKNOWN_REG(i);

        return;
    }
        
    if (show_core) {
        printf("Avivo engine:\n");
        SHOW_REG(AVIVO_ENGINE_STATUS);
    }

    if (show_mc) {
        printf("\nMemory controller:\n");
        SHOW_MC_REG(MC00);
        SHOW_MC_REG(MC01);
        SHOW_MC_REG(MC02);
        SHOW_MC_REG(MC03);
        SHOW_MC_REG(AVIVO_MC_MEMORY_MAP);
        SHOW_MC_REG(MC05);
        SHOW_MC_REG(MC06);
        SHOW_MC_REG(MC07);
        SHOW_MC_REG(MC08);
        SHOW_MC_REG(MC09);
        SHOW_MC_REG(MC0a);
        SHOW_MC_REG(MC0b);
        SHOW_MC_REG(MC0c);
        SHOW_MC_REG(MC0d);
        SHOW_MC_REG(MC0e);
        SHOW_MC_REG(MC0f);
        SHOW_MC_REG(MC10);
        SHOW_MC_REG(MC11);
        SHOW_MC_REG(MC12);
        SHOW_MC_REG(MC13);
        SHOW_MC_REG(MC14);
        SHOW_MC_REG(MC15);
        SHOW_MC_REG(MC16);
        SHOW_MC_REG(MC17);
        SHOW_MC_REG(MC18);
    }

    if (show_crtc1) {
        printf("\nCRTC1:\n");
        SHOW_REG_BITS(AVIVO_CRTC1_CNTL,
                      0, 0, "Enable",
                      8, 8, "Mystery bit #1",
                      16, 16, "Mystery bit #2",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_MODE,
                      8, 8, "Text mode",
                      0, 0, NULL);
        SHOW_REG_DECIMAL(AVIVO_CRTC1_H_TOTAL);
        SHOW_REG_BITS(AVIVO_CRTC1_H_BLANK,
                      0, 15, "DECIMALTotal - Pulse start + disp",
                      16, 31, "DECIMALTotal - disp",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_H_SYNC_WID,
                      16, 23, "DECIMALSync width",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_H_SYNC_POL,
                      0, 0, "Polarity",
                      0, 0, NULL);
        SHOW_REG_DECIMAL(AVIVO_CRTC1_V_TOTAL);
        SHOW_REG_BITS(AVIVO_CRTC1_V_BLANK,
                      0, 15, "DECIMALTotal - Pulse start + disp",
                      16, 31, "DECIMALTotal - disp",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_V_SYNC_WID,
                      16, 23, "DECIMALSync width",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_V_SYNC_POL,
                      0, 0, "Polarity",
                      0, 0, NULL);
        SHOW_REG(AVIVO_CRTC1_SCAN_ENABLE);
        SHOW_REG_BITS(AVIVO_CRTC1_FB_FORMAT,
                      0, 3, "Format",
                      0, 0, NULL);
        SHOW_REG(AVIVO_CRTC1_FB_LOCATION);
        SHOW_REG(AVIVO_CRTC1_FB_END);
        SHOW_REG_DECIMAL(AVIVO_CRTC1_PITCH);
        SHOW_REG_DECIMAL(AVIVO_CRTC1_X_LENGTH);
        SHOW_REG_DECIMAL(AVIVO_CRTC1_Y_LENGTH);
        SHOW_REG_BITS(AVIVO_CRTC1_EXPANSION_CNTL,
                      0, 0, "Enable",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_EXPANSION_SOURCE,
                      16, 31, "DECIMALHeight",
                      0, 15, "DECIMALWidth",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_OFFSET_START,
                      16, 31, "DECIMALx",
                      0, 15, "DECIMALy",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC1_OFFSET_END,
                      16, 31, "DECIMALx",
                      0, 15, "DECIMALy",
                      0, 0, NULL);
    }
    else {
        if (!shut_up)
            printf("\nCRTC1 disabled\n");
    }

    if (show_crtc2) {
        printf("\nCRTC2:\n");
        SHOW_REG_BITS(AVIVO_CRTC2_CNTL,
                      0, 0, "Enable",
                      8, 8, "Mystery bit #1",
                      16, 16, "Mystery bit #2",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC2_MODE,
                      8, 8, "Text mode",
                      0, 0, NULL);
        SHOW_REG_DECIMAL(AVIVO_CRTC2_H_TOTAL);
        SHOW_REG_BITS(AVIVO_CRTC2_H_BLANK,
                      0, 15, "DECIMALTotal - Pulse start + disp",
                      16, 31, "DECIMALTotal - disp",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC2_H_SYNC_WID,
                      16, 23, "DECIMALSync width",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC2_H_SYNC_POL,
                      0, 0, "Polarity",
                      0, 0, NULL);
        SHOW_REG_DECIMAL(AVIVO_CRTC2_V_TOTAL);
        SHOW_REG_BITS(AVIVO_CRTC2_V_BLANK,
                      0, 15, "DECIMALTotal - Pulse start + disp",
                      16, 31, "DECIMALTotal - disp",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC2_V_SYNC_WID,
                      16, 23, "DECIMALSync width",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CRTC2_V_SYNC_POL,
                      0, 0, "Polarity",
                      0, 0, NULL);
        SHOW_REG(AVIVO_CRTC2_SCAN_ENABLE);
        SHOW_REG_BITS(AVIVO_CRTC2_FB_FORMAT,
                      0, 3, "Format",
                      0, 0, NULL);
        SHOW_REG(AVIVO_CRTC2_FB_LOCATION);
        SHOW_REG(AVIVO_CRTC2_FB_END);
        SHOW_REG_DECIMAL(AVIVO_CRTC2_PITCH);
        SHOW_REG_DECIMAL(AVIVO_CRTC2_X_LENGTH);
        SHOW_REG_DECIMAL(AVIVO_CRTC2_Y_LENGTH);
    }
    else {
        if (!shut_up)
            printf("\nCRTC2 disabled\n");
    }

    if (show_dac1) {
        printf("\nDAC1:\n");
        SHOW_REG(AVIVO_DAC1_CNTL);
        SHOW_REG(AVIVO_DAC1_MYSTERY1);
        SHOW_REG(AVIVO_DAC1_MYSTERY2);
    }
    else {
        if (!shut_up)
            printf("\nDAC1 disabled\n");
    }

    if (show_dac2) {
        printf("\nDAC2:\n");
        SHOW_REG(AVIVO_DAC2_CNTL);
        SHOW_REG(AVIVO_DAC2_MYSTERY1);
        SHOW_REG(AVIVO_DAC2_MYSTERY2);
    }
    else {
        if (!shut_up)
            printf("\nDAC2 disabled\n");
    }

    if (show_tmds1) {
        printf("\nTMDSA:\n");
        SHOW_REG_BITS(AVIVO_TMDSA_CNTL,
                      0, 0, "Enable",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_TMDS_STATUS,
                      0, 0, "TMDSA connected",
                      0, 0, NULL);
        SHOW_REG(AVIVO_TMDSA_CLOCK_ENABLE);
        SHOW_REG(AVIVO_TMDSA_TRANSMITTER_ENABLE);
        SHOW_REG(AVIVO_TMDSA_BIT_DEPTH_CONTROL);
        SHOW_REG(AVIVO_TMDSA_DATA_SYNCHRONIZATION);
        SHOW_REG(AVIVO_TMDSA_TRANSMITTER_CONTROL);
    }
    else {
        if (!shut_up)
            printf("\nTMDSA disabled (%spresent)\n",
                   (GET_REG(AVIVO_TMDS_STATUS) &
                    AVIVO_TMDSA_CONNECTED) ? "" : "not ");
    }

    if (show_tmds2) {
        printf("\nLVTMA:\n");
        SHOW_REG_BITS(AVIVO_LVTMA_CNTL,
                      0, 0, "Enable",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_TMDS_STATUS,
                      8, 8, "LVTMA connected",
                      0, 0, NULL);
        SHOW_REG(AVIVO_LVTMA_CLOCK_ENABLE);
        SHOW_REG(AVIVO_LVTMA_TRANSMITTER_ENABLE);
        SHOW_REG(AVIVO_LVTMA_BIT_DEPTH_CONTROL);
        SHOW_REG(AVIVO_LVTMA_DATA_SYNCHRONIZATION);
        SHOW_REG(AVIVO_LVTMA_TRANSMITTER_CONTROL);
    }
    else {
        if (!shut_up)
            printf("\nLVTMA disabled (%spresent)\n",
                   (GET_REG(AVIVO_TMDS_STATUS) &
                    AVIVO_LVTMA_CONNECTED) ? "" : "not ");
    }

    if (show_lvds) {
        printf("\nLVDS:\n");
        SHOW_REG_BITS(AVIVO_LVDS_CNTL,
                      0, 0, "Enable",
                      4, 4, "Enable #2",
                      0, 0, NULL);
        SHOW_REG_BITS(AVIVO_LVDS_BACKLIGHT_CNTL,
                      0, 0, "Backlight control",
                      8, 15, "DECIMALBacklight level",
                      0, 0, NULL);
    }
    else {
        if (!shut_up)
            printf("\nLVDS disabled\n");
    }

    if (show_cur1) {
        printf("\nCursor 1:\n");
        SHOW_REG_BITS(AVIVO_CURSOR1_CNTL, 0, 0, "Enable", 8, 9, "Format (ABGR/ARGB)", 0, 0, NULL);
        SHOW_REG_BITS(AVIVO_CURSOR1_POSITION, 16, 31, "DECIMALx", 0, 15, "DECIMALy", 0, 0, NULL);
        SHOW_REG(AVIVO_CURSOR1_LOCATION);
        SHOW_REG_BITS(AVIVO_CURSOR1_SIZE, 16, 31, "DECIMALx", 0, 15, "DECIMALy", 0, 0, NULL);
    }
    else {
        if (!shut_up)
            printf("\nCursor 1 disabled\n");
    }
}

void radeon_reg_match(const char *pattern)
{
    int i;
    unsigned long address;
    unsigned int value;

    if (pattern[0] == '0' && pattern[1] == 'x') {
        address = strtol(&(pattern[2]), NULL, 16);
        value = radeon_get(address, pattern);
        printf("%s\t0x%08x (%d)\n", pattern, value, value);
    }
    else if (pattern[0] == 'M' && pattern[1] == 'C' && pattern[2] == ':') {
        address = strtol(&(pattern[3]), NULL, 16);
        value = radeon_get_mc(address, pattern);
        printf("%s\t0x%08x (%d)\n", pattern, value, value);
    }
    else {
        for (i = 0; i < sizeof(reg_list) / sizeof(reg_list[0]); i++) {
            if (fnmatch(pattern, reg_list[i].name, 0) == 0) {
                value = reg_list[i].get(reg_list[i].address, reg_list[i].name);
                printf("%s (%s%04x)\t0x%08x (%d)\n", reg_list[i].name,
                       reg_list[i].type, reg_list[i].address, value, value);
            }
        }
    }
}

void set_reg(const char *name, const char *type, unsigned long address,
             unsigned int value,
             unsigned int (*get)(unsigned long, const char *),
             void (*set)(unsigned long, const char *, unsigned int))
{
    unsigned int readback;

    readback = get(address, name);
    printf("OLD: %s (%s%04lx)\t0x%08x (%d)\n", name, type, address,
           readback, readback);
    set(address, name, value);
    readback = get(address, name);
    printf("NEW: %s (%s%04lx)\t0x%08x (%d)\n", name, type, address,
           readback, readback);
}

void radeon_reg_set(const char *inname, unsigned int value)
{
    int i;
    unsigned long address;

    if (inname[0] == '0' && inname[1] == 'x') {
        address = strtol(&(inname[2]), NULL, 16);
        set_reg(inname, "", address, value, radeon_get, radeon_set);
    }
    else if (inname[0] == 'M' && inname[1] == 'C' && inname[2] == ':') {
        address = strtol(&(inname[3]), NULL, 16);
        set_reg(inname, "MC: ", address, value, radeon_get_mc, radeon_set_mc);
    }
    else {
        for (i = 0; i < sizeof(reg_list) / sizeof(reg_list[0]); i++) {
            if (fnmatch(inname, reg_list[i].name, 0) == 0) {
                set_reg(reg_list[i].name, reg_list[i].type, reg_list[i].address,
                        value, reg_list[i].get, reg_list[i].set);
            }
        }
    }
}

/* Find and map the buffers. */
static void map_radeon_mem(void)
{
#if 0
    struct pci_id_match match;
#else
    struct pci_slot_match match;
#endif
    struct pci_device_iterator *iter;
    struct pci_device *device;
    int i = 0;

    if (pci_system_init() != 0)
        fatal("failed to initialise libpciaccess\n");

#if 0
    match.vendor_id = 0x1002;
    match.device_id = PCI_MATCH_ANY;
    match.subvendor_id = PCI_MATCH_ANY;
    match.subdevice_id = PCI_MATCH_ANY;
    match.device_class = (0x03 << 16);
    match.device_class_mask = 0x00ff0000;
    match.match_data = 0;
    iter = pci_id_match_iterator_create(&match);
#else
    match.domain = PCI_MATCH_ANY;
    match.bus = PCI_MATCH_ANY;
    match.dev = PCI_MATCH_ANY;
    match.func = 0;
    match.match_data = 0;
    iter = pci_slot_match_iterator_create(&match);
#endif

    while ((device = pci_device_next(iter))) {
        pci_device_probe(device);
        if (device->vendor_id == 0x1002 &&
            (device->device_class & 0x00ffff00) == 0x00030000) {
            if (debug) {
                printf("Found card %x:%x (%x)\n", device->vendor_id,
                       device->device_id, device->device_class);
                switch (device->device_id) {
                case 0x724b:
                    printf("  r580 (Radeon X1900 GT)\n");
                    break;
                default:
                    printf("  (unknown card)\n");
                    break;
                }
            }

            for (i = 0; i < 6; i++) {
                if (device->regions[i].size == 64 * 1024)
                    ctrl_region = i;
                else if (device->regions[i].size >= 128 * 1024 * 1024)
                    fb_region = i;
            }
            avivo_device = device;
        }
    }

    if (!avivo_device)
        fatal("cannot find Radeon device\n");

    if (pci_device_map_region(avivo_device, ctrl_region, 1) != 0)
        fatal("mapping ctrl region\n");
    ctrl_mem = avivo_device->regions[ctrl_region].memory;

    if (pci_device_map_region(avivo_device, fb_region, 1) != 0)
        fatal("mapping fb region\n");
    fb_mem = avivo_device->regions[fb_region].memory;

    pci_iterator_destroy(iter);

    if (debug)
        printf("Radeon found. Base control address is %lx; "
               "base framebuffer address is %lx.\n",
               (unsigned long) ctrl_mem, (unsigned long) fb_mem);
}

#ifdef __powerpc__
#define __swab16(x) \
({ \
        unsigned short __x = (x); \
        ((unsigned short) ( \
                (((unsigned short) (__x) & (unsigned short) 0x00ffU) << 8) | \
                (((unsigned short) (__x) & (unsigned short) 0xff00U) >> 8) )); \
})
#define __swab32(x) \
({ \
    unsigned int __x = (x); \
    ((unsigned int) ( \
        (((unsigned int) (__x) & (unsigned int) 0x000000ffUL) << 24) | \
        (((unsigned int) (__x) & (unsigned int) 0x0000ff00UL) <<  8) | \
        (((unsigned int) (__x) & (unsigned int) 0x00ff0000UL) >>  8) | \
        (((unsigned int) (__x) & (unsigned int) 0xff000000UL) >> 24) )); \
})
#define BIOS16(offset)    __swab16(*((unsigned short *) (bios + (offset))))
#define BIOS32(offset)    __swab32(*((unsigned int *) (bios + (offset))))
#else
#define BIOS16(offset)    (*((unsigned short *) (bios + (offset))))
#define BIOS32(offset)    (*((unsigned int *) (bios + (offset))))
#endif
#define BIOS8(offset)   (*((unsigned char *) (bios + (offset))))

struct nametable_entry
{
    unsigned int value;
    const char *name;
};

const char *radeon_valname(const struct nametable_entry *table, unsigned int value)
{
    static char ret_buf[256];

    while (table->name) {
        if (table->value == value)
            return table->name;
        table++;
    }

    sprintf(ret_buf, "<unknown val %d>", value);
    return ret_buf;
}

static struct nametable_entry hdr_type_name[] = {
    { 2, "Rage128 & Pro" },
    { 3, "Rage M3" },
    { 4, "Radeon" },
    { 36, "ATOM" },
    { 0, NULL }
};

static void radeon_rom_legacy_clocks(unsigned char *bios, int hdr)
{
    int pll_info_block = BIOS16(hdr + 0x30);

    if (pll_info_block == 0) {
        printf("No clock info block in BIOS\n");
        return;
    }

    printf("Clock info block:\n");
    printf("  SCLK   : %f\n", BIOS16(pll_info_block + 0x08) / 100.0);
    printf("  MCLK   : %f\n", BIOS16(pll_info_block + 0x0a) / 100.0);
    printf("  RefClk : %f\n", BIOS16(pll_info_block + 0x0e) / 100.0);
    printf("  RefDiv : %d\n", BIOS16(pll_info_block + 0x10));
    printf("  VCO Min: %f\n", BIOS32(pll_info_block + 0x12) / 100.0);
    printf("  VCO Max: %f\n", BIOS32(pll_info_block + 0x16) / 100.0);
    printf("\n");
}

static void radeon_rom_atom_clocks(unsigned char *bios, int master)
{

    int pll_info_block = BIOS16(master + 12);

    if (pll_info_block == 0) {
        printf("No clock info block in BIOS\n");
        return;
    }

    printf("Clock info block:\n");
    printf("  SCLK    : %f\n", BIOS32(pll_info_block + 8) / 100.0);
    printf("  MCLK    : %f\n", BIOS32(pll_info_block + 12) / 100.0);
    printf("  RefClk  : %f\n", BIOS16(pll_info_block + 82) / 100.0);
    printf("  PPLL Min: %f\n", BIOS16(pll_info_block + 78) / 100.0);
    printf("  PPLL Max: %f\n", BIOS32(pll_info_block + 32) / 100.0);
}


static struct nametable_entry lconn_type_name[] = {
    { 0, "None" },
    { 1, "Proprietary" },
    { 2, "VGA" },
    { 3, "DVI-I" },
    { 4, "DVI-D" },
    { 5, "CTV" },
    { 6, "STV" },
    { 7, "Unsupported" },
    { 0, NULL }
};

static struct nametable_entry lddc_type_name[] = {
    { 0, "None" },
    { 1, "MONID" },
    { 2, "DVI" },
    { 3, "VGA" },
    { 4, "CRT2" },
    { 5, "AVIVO connector #1?" },
    { 6, "AVIVO connector #2?" },
    { 7, "AVIVO LVDS connector #3?" },
    { 0, NULL }
};

static struct nametable_entry ldac_type_name[] = {
    { -1, "None" },
    { 0, "CRT" },
    { 1, "TV" },
    { 2, "External" },
    { 0, NULL }
};

static void radeon_rom_legacy_connectors(unsigned char *bios, int hdr)
{
    int offset = BIOS16(hdr + 0x50);
    int i, entry, tmp, chips, entries;

    if (offset == 0) {
        printf("No connector table in BIOS\n");
        return;
    }

    printf("Connector table:\n");

#if 0
    printf("  raw: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           BIOS8(offset+0), BIOS8(offset+1), BIOS8(offset+2),
           BIOS8(offset+3), BIOS8(offset+4), BIOS8(offset+5),
           BIOS8(offset+6), BIOS8(offset+7));
#endif

    chips = BIOS8(offset) >> 4; 
    printf("  Table revision %d for %d chip(s)\n",
           BIOS8(offset) & 0xf, chips);
    if (chips > 1)
        printf("  Only 1 chip supported for now !\n");
    entries = BIOS8(offset + 1) & 0xf;
    printf("  Table for chip %d has %d connector(s):\n",
           BIOS8(offset + 1) >> 4, entries);

    for (i = 0; i < 4; i++) {
        entry = offset + 2 + i*2;

        /* End of table */
        if (!BIOS16(entry)) {
            if (i < entries)
                printf("    <table early termination !>\n");
            break;
        }

        /* Read table entry, check connector type */
        tmp = BIOS16(entry);
        printf("    %08x  ", tmp);
        printf("Type: %s", radeon_valname(lconn_type_name,
                         (tmp >> 12) & 0xf));
        printf(", DDC: %s", radeon_valname(lddc_type_name,
                          (tmp >> 8) & 0xf));
        printf(", DAC: %s", radeon_valname(ldac_type_name, tmp & 0x3));
        printf(", TMDS: %s", (tmp & 0x10) ? "External" : "Internal");
        
        printf("\n");
    }
    printf("\n");
}

static struct nametable_entry atomconn_type_name[] = {
    { 0, "None" },
    { 1, "VGA" },
    { 2, "DVI-I" },
    { 3, "DVI-D" },
    { 4, "DVI-A" },
    { 5, "STV" },
    { 6, "CTV" },
    { 7, "LVDS" },
    { 8, "Digital" },
    { 9, "Unsupported" },
    { 0, NULL }
};

static void radeon_rom_atom_connectors(unsigned char *bios, int master)
{
    int offset = BIOS16(master + 22);
    int tmp, i, tmp0;
    int crtc, dac, connector, ddc=0;

    if (offset == 0) {
        printf("No connector table in BIOS\n");
        return;
    }

    tmp = BIOS16(offset + 4);
    printf("Connector table:\n");

    for (i = 0; i < 8; i++) {
        if (tmp & (1 << i)) {
            int gpio;
            int portinfo = BIOS16(offset + 6 + i * 2);

            crtc = (portinfo >> 8) & 0xf;
            dac = (portinfo & 0xf) - 1;
            connector = (portinfo >> 4) & 0xf;

            tmp0 = BIOS16(master + 24);
            if (1 /* crtc */) {
                gpio = BIOS16(tmp0 + 4 + 27 * crtc) * 4;
                switch(gpio)
                {
                case RADEON_GPIO_MONID: ddc = 1; break;
                case RADEON_GPIO_DVI_DDC: ddc = 2; break;
                case RADEON_GPIO_VGA_DDC: ddc = 3; break;
                case RADEON_GPIO_CRT2_DDC: ddc = 4; break;
                case AVIVO_GPIO_0: ddc = 7; break;
                case AVIVO_GPIO_1: ddc = 5; break;
                case AVIVO_GPIO_2: ddc = 6; break;
                case AVIVO_GPIO_3: ddc = 7; break;
                default: ddc = 0; break;
                }
            }

            printf("%d:    %08x ", i, portinfo);
            printf(", Id: %d", crtc);
            printf(", Type: %s",  radeon_valname(atomconn_type_name,
                                                 connector));
            if (1 /* crtc */)
                printf(", DDC: %s", radeon_valname(lddc_type_name, ddc));
            /* On AVIVO cards, the DAC is unset for TMDS */
            if (dac >= 0 || (i != 3 && i != 7))
                printf(", DAC: %s", radeon_valname(ldac_type_name, dac));
            printf(", GPIO: 0x%04X", gpio);
            if (i == 3)
                printf(" TMDS: Internal\n");
            else if (i == 7)
                printf(" TMDS: External\n");
            else
                printf("\n");
                
        }
    }
}

static void radeon_rom_atom_tmds_pll(unsigned char *bios, int master)
{
    int offset, tmp, tmp0;
    int i;

    offset = BIOS16(master + 18);
    if (offset) {
        printf("TMDS PLLs:\n");
        /* As far as I can tell, these are in hecto Hertz (i.e. e2).
         * Yes, this is weird. */
        tmp = BIOS16(offset + 4);
        printf("Maximum frequency: %dHz\n", tmp * 10);
        
        for (i = 0; i < 4; i++) {
            tmp = BIOS16(offset + (i * 6) + 6);
            tmp0 = (BIOS8(offset + (i * 8) + 8) & 0x3f) |
                   ((BIOS8(offset + (i * 8) + 10) & 0x3f) << 6) |
                   ((BIOS8(offset + (i * 8) + 9) & 0xf) << 12) |
                   ((BIOS8(offset + (i * 8) + 11) & 0xf) << 16);
            printf("    %d: %dHz %x\n", i, tmp * 10, tmp0);
        }
    }
    else {
        printf("No TMDS PLLs\n");
    }
}

static void radeon_rom_atom_lvds(unsigned char *bios, int master)
{
    int offset;

    offset = BIOS16(master + 16);
    if (offset) {
        printf("LVDS timings:\n");
        printf("  x: %d, y: %d, dotclock: %d\n",
               BIOS16(offset + 6), BIOS16(offset + 10),
               BIOS16(offset + 4) * 10);
        printf("  hblank: %d, hoverplus: %d, hsyncwidth: %d\n",
               BIOS16(offset + 8), BIOS16(offset + 14), BIOS16(offset + 16));
        printf("  vblank: %d, voverplus: %d, vsyncwidth: %d\n",
               BIOS16(offset + 12), BIOS16(offset + 18), BIOS16(offset + 20));
        printf("  power-on delay: %d\n", BIOS16(offset + 40));
    }
    else {
        printf("No LVDS\n");
    }
}

static void radeon_rom_legacy_dfptable(unsigned char *bios, int hdr)
{
    int offset, i, n, rev, stride;

    offset = BIOS16(hdr + 0x34);
    if (offset == 0) {
        printf("No DFP info table\n");
        return;
    }

    rev = BIOS8(offset);
    printf("DFP table revision: %d\n", rev);

    switch(rev) {
    case 3:
        n = BIOS8(offset + 5) + 1;
        if (n > 4)
            n = 4;
        for (i = 0; i < n; i++) {
            /* Looks weird ... but that's what is in X.org */
            printf("  PixClock: %f\t TMDS_PLL_CNTL: %08x\n",
                   BIOS16(offset+i*10+0x10) / 100.0,
                   BIOS32(offset+i*10+0x08));
        }
        break;

    /* revision 4 has some problem as it appears in RV280...
     */
    case 4:
        stride = 0;
        n = BIOS8(offset+ 5) + 1;
        if (n > 4)
            n = 4;
        for (i = 0; i < n; i++) {
            printf("  PixClock: %f\t TMDS_PLL_CNTL: %08x\n",
                   BIOS16(offset+stride+0x10) / 100.0,
                   BIOS32(offset+stride+0x08));
            if (i == 0)
                stride += 10;
            else
                stride += 6;
        }
        break;
    }
}


void radeon_rom_tables(const char * file)
{
#define _64K (64*1024)
    unsigned char bios[_64K];
    char *biosmem;
    int fd, hdr, atom;

    if (strcmp(file, "mmap") == 0) {
        fd = open("/dev/mem", O_RDWR);
        biosmem = mmap(0, _64K, PROT_READ, MAP_SHARED, fd, 0xc0000);
        if ((long) biosmem <= 0) {
            perror("can't mmap bios");
            return;
        }
        memset(bios, 0, _64K);
        memcpy(bios, biosmem, _64K);
        munmap(biosmem, _64K);
        close(fd);
    }
    else {
        fd = open(file, O_RDONLY);
        if (fd < 0) {
            perror("can't open rom file");
            return;
        }
        memset(bios, 0, _64K);
        read(fd, bios, _64K);
        close(fd);
    }

    if (bios[0] != 0x55 || bios[1] != 0xaa)
        fatal("PCI ROM signature 0x55 0xaa missing\n");

    hdr = BIOS16(0x48);
    printf("\nBIOS Tables:\n------------\n\n");	
    printf("Header at %x, type: %d [%s]\n", hdr, BIOS8(hdr),
           radeon_valname(hdr_type_name, BIOS8(hdr)));
    printf("OEM ID: %02x %02x\n", BIOS8(hdr+2), BIOS8(hdr+3));
    atom = (BIOS8(hdr+4)   == 'A' &&
        BIOS8(hdr+5) == 'T' &&
        BIOS8(hdr+6) == 'O' &&
        BIOS8(hdr+7) == 'M') ||
        (BIOS8(hdr+4)   == 'M' &&
         BIOS8(hdr+5) == 'O' &&
         BIOS8(hdr+6) == 'T' &&
         BIOS8(hdr+7) == 'A');

    if (atom) {
        int master = BIOS16(hdr+32);
        printf("ATOM BIOS detected !\n\n");
        radeon_rom_atom_clocks(bios, master);
        radeon_rom_atom_connectors(bios, master);
        radeon_rom_atom_tmds_pll(bios, master);
        radeon_rom_atom_lvds(bios, master);
        // add more ...
    }
    else {
        printf("Legacy BIOS detected !\n");
        printf("BIOS Rev: %x.%x\n\n", BIOS8(hdr+4), BIOS8(hdr+5));
        radeon_rom_legacy_clocks(bios, hdr);
        radeon_rom_legacy_connectors(bios, hdr);
        radeon_rom_legacy_dfptable(bios, hdr);
    }
}

int main(int argc, char *argv[]) 
{
    if (argc == 1)
        usage();

    if (strcmp(argv[1], "--debug") == 0) {
        debug = 1;
        argv++;
        argc--;
    }

    if (strcmp(argv[1], "--skip=") == 0) {
        skip = atoi(argv[1] + 7);
        argv++;
        argc--;
    }

    map_radeon_mem();

    if (argc == 2) {
        if (strcmp(argv[1], "regs") == 0) {
            radeon_cmd_regs("default");
            return 0;
        }
        if (strcmp(argv[1], "i2c") == 0) {
            radeon_i2c();
            return 0;
        }
        if (strcmp(argv[1], "i2c-monitor") == 0) {
            radeon_i2c_monitor_default();
            return 0;
        }
    }
    else if (argc == 3) {
        if (strcmp(argv[1], "regmatch") == 0) {
            radeon_reg_match(argv[2]);
            return 0;
        }
        else if (strcmp(argv[1], "romtables") == 0) {
            radeon_rom_tables(argv[2]);
            return 0;
        }
        else if (strcmp(argv[1], "regs") == 0) {
            radeon_cmd_regs(argv[2]);
            return 0;
        }
        else if (strcmp(argv[1], "dumpimg") == 0) {
            radeon_dump_img(argv[2]);
            return 0;
        }
        else if (strcmp(argv[1], "loadimg") == 0) {
            radeon_load_img(argv[2]);
            return 0;
        }
    }
    else if (argc == 4) {
        if (strcmp(argv[1], "regset") == 0) {
            radeon_reg_set(argv[2], strtoul(argv[3], NULL, 0));
            return 0;
        }
        if (strcmp(argv[1], "output") == 0) {
            radeon_output_set(argv[2], argv[3]);
            return 0;
        }
        if (strcmp(argv[1], "i2c-monitor") == 0) {
            int gpioin = strtol(argv[2], (char **)NULL, 16);
            int gpioout = strtol(argv[3], (char **)NULL, 16);
            if (gpioin < 0) {
                fprintf(stderr, "GPIO_IN address < 0\n");
                return 1;
            }
            if (gpioout < 0) {
                fprintf(stderr, "GPIO_OUT address < 0\n");
                return 1;
            }
            radeon_i2c_monitor(gpioin, gpioout);
            return 0;
        }
    }

    usage();

    pci_system_cleanup();

    return 1;
}
