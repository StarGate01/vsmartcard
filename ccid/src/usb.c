/*
 * Copyright (C) 2009-2012 Frank Morgner <frankmorgner@gmail.com>
 *
 * This file is part of ccid-emulator.
 *
 * ccid-emulator is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * ccid-emulator is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ccid-emulator.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>

#include <linux/types.h>

#include "ccid.h"
#include "cmdline.h"
#include "config.h"
#include "scutil.h"
#include "usbstring.h"

static int verbose = 0;
static int debug = 0;
static const char *devicefile = "/dev/ccidg0";

static pthread_t ccid_thread;
static int device_fd = -1;

/* ccid thread, forwards ccid requests to pcsc and returns results  */
static void *ccid(void)
{
	int result;
	size_t bufsize = 512; // sizeof(PC_to_RDR_XfrBlock_t) + CCID_EXT_APDU_MAX;

	__u8 inbuf[bufsize];
	__u8 *outbuf = NULL;

	do
	{
		if (verbose > 1)
			fprintf(stderr, "bulk loop: reading %lu bytes... ", (long unsigned)bufsize);
			
		result = read(device_fd, inbuf, bufsize);

		if (result < 0)
			break;
		if (verbose > 1)
			fprintf(stderr, "bulk loop: got %d, done.\n", result);
		if (!result)
			break;

		result = ccid_parse_bulkout(inbuf, result, &outbuf);

		if (result < 0)
			break;

		if (verbose > 1)
			fprintf(stderr, "bulk loop: writing %d bytes... ", result);

		result = write(device_fd, outbuf, result);

		if (verbose > 1)
			fprintf(stderr, "done (%d written).\n", result);
	} while (result >= 0);

	if (errno != ESHUTDOWN || result < 0)
	{
		perror("ccid loop aborted");
	}

	if(outbuf != NULL) 
		free(outbuf);

	fflush(stdout);
	fflush(stderr);

	return 0;
}

int main(int argc, char **argv)
{
	/*printf("%s:%d\n", __FILE__, __LINE__);*/

	struct gengetopt_args_info cmdline;

	/* Parse command line */
	if (cmdline_parser(argc, argv, &cmdline) != 0)
		exit(1);
	verbose = cmdline.verbose_given;
	devicefile = cmdline.device_arg;

	if (cmdline.info_flag)
		return print_avail(verbose);

	if (ccid_initialize(cmdline.reader_arg, verbose) < 0)
	{
		fprintf(stderr, "Can't initialize ccid\n");
		return 1;
	}

	device_fd = open(devicefile, O_RDWR);
	if (device_fd < 0)
		return 1;
	if (debug)
		fprintf(stderr, "%s opened\n", devicefile);

	fflush(stdout);
	fflush(stderr);
	cmdline_parser_free(&cmdline);

	ccid();
	
	ccid_shutdown();

	if(device_fd != -1) 
		close(device_fd);

	return 0;
}
