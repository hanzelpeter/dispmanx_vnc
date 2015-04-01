// VNC server using dispmanx
// TODO: mouse support with inconsistency between fbdev and dispmanx

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>

#include "Exception.hh"
#include "UFile.hh"
#include "DMXResource.hh"
#include "DMXDisplay.hh"
#include "DMXVNCServer.hh"
#define BPP      2

/* 15 frames per second (if we can) */
#define PICTURE_TIMEOUT (1.0/15.0)

extern bool terminate;

void usage(const char *programName);

void sig_handler(int signo)
{
	terminate = true;
}

int main(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	try
	{
		uint32_t screen = 0;
		bool relativeMode = 0;
		std::string password;
		int port = 0;
		bool safeMode = true;
		bool bandwidthMode = true;

		static struct option long_options[] = {
			{ "relative", no_argument, nullptr, 'r' },
			{ "absolute", no_argument, nullptr, 'a' },
			{ "unsafe", no_argument, nullptr, 'u' },
			{ "fullscreen", no_argument, nullptr, 'f' },
			{ "password", required_argument, nullptr, 'P' },
			{ "port", required_argument, nullptr, 'p' },
			{ "screen", required_argument, nullptr, 's' },
			{ "help", no_argument, nullptr, CHAR_MIN - 2 },
			{ nullptr, 0, nullptr, 0}
		};

		int c;
		while (-1 != (c = getopt_long(argc, argv, "abfP:p:rs:u", long_options, nullptr))) {
			switch (c) {
			case 'a':
				relativeMode = false;
				break;

			case 'r':
				relativeMode = true;
				break;

			case 'u':
				safeMode = false;
				break;

			case 'f':
				bandwidthMode = false;
				break;

			case 'P':
				password = optarg;
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 's':
				screen = atoi(optarg);
				break;

			case CHAR_MIN - 2:
				throw HelpException();
				break;

			default:
				throw ParamException();
			}
		}

		if (signal(SIGINT, sig_handler) == SIG_ERR) {
			throw Exception( "error setting sighandler");
		}

		DMXVNCServer vncServer(BPP,PICTURE_TIMEOUT);
		vncServer.Run( argc, argv, port, password, screen, relativeMode, safeMode, bandwidthMode);
	}
	catch (HelpException) {
		usage(argv[0]);
	}
	catch (ParamException) {
		std::cerr << "Try '" << argv[0] << "' --help for more information.\n";
		ret = EXIT_FAILURE;
	}
	catch (Exception& e) {
		std::cerr << "Exception caught: " << e.what() << "\n";
		ret = EXIT_FAILURE;
	}

	return ret;
}

void usage(const char *programName)
{
	std::cout << 
		"Usage: " << programName << " [OPTION]...\n"
		"\n"
		"  -a, --absolute               absolute mouse movements\n"
		"  -f, --fullscreen             always runs fullscreen mode\n"
		"  -p, --port=PORT              makes vnc available on the speficied port\n"
		"  -P, --password=PASSWORD      protects the session with PASSWORD\n"
		"  -r, --relative               relative mouse movements\n"
		"  -s, --screen=SCREEN          opens the specified screen number\n"
		"  -u, --unsafe                 disables more robust handling of resolution\n"
		"                               change at a small performance gain\n"
		"      --help                   displays this help and exit\n";
}
