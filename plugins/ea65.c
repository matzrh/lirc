/*
 * Support for builtin key panel and remote control on
 *      AOpen XC Cube EA65, EA65-II
 *
 * Copyright (C) 2004 Max Krasnyansky <maxk@qualcomm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef LIRC_IRTTY
#define LIRC_IRTTY "/dev/ttyS1"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lirc_driver.h"
#include "lirc/serial.h"

#define TIMEOUT     60000
#define CODE_LENGTH 24

struct timeval start, end, last;
ir_code code;

//Forwards:
int ea65_decode(struct ir_remote *remote,
		ir_code * prep, ir_code * codep, ir_code * postp,
		int *repeat_flagp,
		lirc_t * min_remaining_gapp, lirc_t * max_remaining_gapp);
int ea65_init(void);
int ea65_release(void);
char *ea65_receive(struct ir_remote *remote);


const struct driver hw_ea65 = {
	.name		=	"ea65",
	.device		=	LIRC_IRTTY,
	.features	=	LIRC_CAN_REC_LIRCCODE,
	.send_mode	=	0,
	.rec_mode	=	LIRC_MODE_LIRCCODE,
	.code_length	=	CODE_LENGTH,
	.init_func	=	ea65_init,
	.deinit_func	=	ea65_release,
	.send_func	=	NULL,
	.rec_func	=	ea65_receive,
	.decode_func	=	ea65_decode,
	.drvctl_func	=	NULL,
	.readdata	=	NULL,
	.api_version	=	2,
	.driver_version = 	"0.9.2"
};

const struct driver* hardwares[] = { &hw_ea65, (const struct driver*)NULL };


int ea65_decode(struct ir_remote *remote, ir_code * prep, ir_code * codep, ir_code * postp, int *repeat_flagp,
		lirc_t * min_remaining_gapp, lirc_t * max_remaining_gapp)
{
	lirc_t d = 0;

	if (!map_code(remote, prep, codep, postp, 0, 0, CODE_LENGTH, code, 0, 0))
		return 0;

	if (start.tv_sec - last.tv_sec >= 2) {
		*repeat_flagp = 0;
	} else {
		d = (start.tv_sec - last.tv_sec) * 1000000 + start.tv_usec - last.tv_usec;
		if (d < 960000) {
			*repeat_flagp = 1;
		} else {
			*repeat_flagp = 0;
		}
	}

	*min_remaining_gapp = 0;
	*max_remaining_gapp = 0;

	return 1;
}

int ea65_init(void)
{
	logprintf(LIRC_INFO, "EA65: device %s", drv.device);

	if (!tty_create_lock(drv.device)) {
		logprintf(LIRC_ERROR, "EA65: could not create lock files");
		return 0;
	}

	drv.fd = open(drv.device, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (drv.fd < 0) {
		logprintf(LIRC_ERROR, "EA65: could not open %s", drv.device);
		tty_delete_lock();
		return 0;
	}

	if (!tty_reset(drv.fd)) {
		logprintf(LIRC_ERROR, "EA65: could not reset tty");
		ea65_release();
		return 0;
	}

	if (!tty_setbaud(drv.fd, 9600)) {
		logprintf(LIRC_ERROR, "EA65: could not set baud rate");
		ea65_release();
		return 0;
	}

	return 1;
}

int ea65_release(void)
{
	close(drv.fd);
	tty_delete_lock();

	return 1;
}

char *ea65_receive(struct ir_remote *remote)
{
	uint8_t data[5];
	int r;

	last = end;
	gettimeofday(&start, NULL);

	if (!waitfordata(TIMEOUT)) {
		logprintf(LIRC_ERROR, "EA65: timeout reading code data");
		return NULL;
	}

	r = read(drv.fd, data, sizeof(data));
	if (r < 4) {
		logprintf(LIRC_ERROR, "EA65: read failed. %s(%d)", strerror(r), r);
		return NULL;
	}

	LOGPRINTF(1, "EA65: data(%d): %02x %02x %02x %02x %02x", r, data[0], data[1], data[2], data[3], data[4]);

	if (data[0] != 0xa0)
		return NULL;

	switch (data[1]) {
	case 0x01:
		if (r < 5)
			return NULL;
		code = (data[2] << 16) | (data[3] << 8) | data[4];
		break;

	case 0x04:
		code = (0xff << 16) | (data[2] << 8) | data[3];
		break;
	}
	logprintf(LIRC_INFO, "EA65: receive code: %llx", (__u64) code);

	gettimeofday(&end, NULL);

	return decode_all(remote);
}
