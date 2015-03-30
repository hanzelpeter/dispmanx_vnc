// VNC server using dispmanx
// TODO: mouse support with inconsistency between fbdev and dispmanx

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include "bcm_host.h"
#include <assert.h>

#include "Exception.hh"
#include "UFile.hh"
#include "DMXResource.hh"
#include "DMXDisplay.hh"
#include "DMXVNCServer.hh"
#define BPP      2

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

/* 15 frames per second (if we can) */
#define PICTURE_TIMEOUT (1.0/15.0)

extern int terminate;

void sig_handler(int signo)
{
	terminate = 1;
}

int main(int argc, char *argv[])
{
	try
	{
		uint32_t screen = 0;
		int relativeMode = 0;
		const char *password = "";
		int port = 0;
		bool safeMode = false;
		bool bandwidthMode = false;

		for (int x = 1; x < argc; x++) {
			if (strcmp(argv[x], "-r") == 0)
				relativeMode = 1;
			if (strcmp(argv[x], "-a") == 0)
				relativeMode = 0;
			if (strcmp(argv[x], "-f") == 0)
				safeMode = true;
			if (strcmp(argv[x], "-b") == 0)
				bandwidthMode = true;
			if (strcmp(argv[x], "-P") == 0) {
				password = argv[x + 1];
				x++;
			}
			if (strcmp(argv[x], "-p") == 0) {
				port = atoi(argv[x + 1]);
				x++;
			}
			if (strcmp(argv[x], "-s") == 0) {
				screen = atoi(argv[x + 1]);
				x++;
			}
		}

		if (signal(SIGINT, sig_handler) == SIG_ERR) {
			fprintf(stderr, "error setting sighandler\n");
			exit(-1);
		}

		DMXVNCServer vncServer(BPP,PICTURE_TIMEOUT);
		vncServer.Run( argc, argv, port, password, screen, relativeMode, safeMode, bandwidthMode);
	}
	catch (Exception& e)
	{
		std::cerr << "Exception caught: " << e.what() << "\n";
	}

	printf("\nDone\n");

	return 0;

}

