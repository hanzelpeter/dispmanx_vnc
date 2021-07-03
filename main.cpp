// VNC server using dispmanx
// TODO: mouse support with inconsistency between fbdev and dispmanx

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <sstream>

#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>

#include <libconfig.h++>

#include "Exception.hpp"
#include "UFile.hpp"
#include "DMXResource.hpp"
#include "DMXDisplay.hpp"
#include "DMXVNCServer.hpp"
#include "Logger.hpp"

extern bool terminate;

struct ConfigData
{
	uint32_t screen = 0;
	bool relative = false;
	std::string password;
	int port = 0;
	bool downscale = false;
	bool unsafe = false;
	bool fullscreen = false;
	bool multiThreaded = false;
	bool localhost = false;
	bool inetd = false;
	int frameRate = 15;
	std::string configFile;
	std::string vncParams;
};

void GetConfigData(int argc, char *argv[], ConfigData& configData);
void GetCommandLineConfigData(int argc, char *argv[], ConfigData& configData);
bool ReadConfigFile(const char *programName, const std::string& configFile, ConfigData& configData);
bool TryReadConfigFile(libconfig::Config& config, const std::string& file);
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
		Logger::SetDefaultModule("dmxvnc");
		
		if (signal(SIGINT, sig_handler) == SIG_ERR) {
			throw Exception( "error setting sighandler");
		}
		if (signal(SIGTERM, sig_handler) == SIG_ERR) {
			throw Exception("error setting sighandler");
		}

		ConfigData configData;
		GetConfigData(argc, argv, configData);

		Logger::Get() <<
			"Running vnc server with the following settings\n"
			"\tframe-rate = " << configData.frameRate << "\n"
			"\tdownscale = " << (configData.downscale ? "true" : "false") << "\n"
			"\tfullscreen = " << (configData.fullscreen ? "true" : "false") << "\n"
			"\tlocalhost = " << (configData.localhost ? "true" : "false") << "\n"
			"\tinetd = " << (configData.inetd ? "true" : "false") << "\n"
			"\tmulti-threaded = " << (configData.multiThreaded ? "true" : "false") << "\n"
			"\tpassword = " << (configData.password.length() ? "***" : "") << "\n"
			"\tport = " << configData.port << "\n"
			"\trelative = " << (configData.relative ? "true" : "false") << "\n"
			"\tscreen = " << configData.screen << "\n"
			"\tunsafe = " << (configData.unsafe ? "true" : "false") << "\n"
			"\tvnc-params = " << configData.vncParams;

		DMXVNCServer vncServer(configData.frameRate);
		vncServer.Run(configData.port, configData.password, configData.screen,
									configData.relative, !configData.unsafe, !configData.fullscreen,
									configData.multiThreaded, configData.downscale,
									configData.localhost,
									configData.inetd,
									configData.vncParams);
	}
	catch (HelpException&) {
		usage(argv[0]);
	}
	catch (ParamException&) {
		Logger::Get() << "Try '" << argv[0] << "' --help for more information";
		ret = EXIT_FAILURE;
	}
	catch (Exception& e) {
		Logger::Get() << "Exception: " << e.what();
		ret = EXIT_FAILURE;
	}

	return ret;
}

void GetConfigData(int argc, char *argv[], ConfigData& configData)
{
	ConfigData configDataTemp;
	GetCommandLineConfigData(argc, argv, configDataTemp);
	if (ReadConfigFile(argv[0], configDataTemp.configFile, configData))
		GetCommandLineConfigData(argc, argv, configData);
	else
		configData = configDataTemp;
}

void GetCommandLineConfigData(int argc, char *argv[], ConfigData& configData)
{
	static struct option long_options[] = {
		{ "relative", no_argument, nullptr, 'r' },
		{ "absolute", no_argument, nullptr, 'a' },
		{ "config-file", required_argument, nullptr, 'c' },
		{ "downscale", no_argument, nullptr, 'd' },
		{ "unsafe", no_argument, nullptr, 'u' },
		{ "fullscreen", no_argument, nullptr, 'f' },
		{ "localhost", no_argument, nullptr, 'l' },
		{ "inetd", no_argument, nullptr, 'i' },
		{ "multi-threaded", no_argument, nullptr, 'm' },
		{ "password", required_argument, nullptr, 'P' },
		{ "port", required_argument, nullptr, 'p' },
		{ "screen", required_argument, nullptr, 's' },
		{ "frame-rate", required_argument, nullptr, 't' },
		{ "vnc-params", required_argument, nullptr, 'v' },
		{ "help", no_argument, nullptr, CHAR_MIN - 2 },
		{ nullptr, 0, nullptr, 0 }
	};

	int c;
	optind = 1;
	while (-1 != (c = getopt_long(argc, argv, "abc:dfilmP:p:rs:t:uv:", long_options, nullptr))) {
		switch (c) {
		case 'a':
			configData.relative = false;
			break;

		case 'r':
			configData.relative = true;
			break;

		case 'c':
			configData.configFile = optarg;
			break;

		case 'd':
			configData.downscale = true;
			break;

		case 'u':
			configData.unsafe = true;
			break;

		case 'f':
			configData.fullscreen = true;
			break;

		case 'l':
			configData.localhost = true;
			break;

		case 'i':
			configData.inetd = true;
			break;

		case 'm':
			configData.multiThreaded = true;
			break;

		case 'P':
			configData.password = optarg;
			break;

		case 'p':
			configData.port = atoi(optarg);
			break;

		case 's':
			configData.screen = atoi(optarg);
			break;

		case 't':
			configData.frameRate = atoi(optarg);
			break;

		case 'v':
			configData.vncParams = optarg;
			break;

		case CHAR_MIN - 2:
			throw HelpException();

		default:
			throw ParamException();
		}
	}

	if (optind < argc) {
		Logger::Get() << "Unknown parameter: " << argv[optind];
		throw ParamException();
	}
}

bool ReadConfigFile(const char *programName, const std::string& configFile, ConfigData& configData)
{
	std::string configFileTemp;
	libconfig::Config config;
	bool readConfig = false;

	if (configFile.length()) {
		configFileTemp = configFile;
		readConfig = TryReadConfigFile(config, configFileTemp);
		if (!readConfig)
			throw Exception("Unable to read the specified configuration file");
	}
	else {
		configFileTemp = programName;
		configFileTemp += ".conf";
		readConfig = TryReadConfigFile(config, configFileTemp);
		if (!readConfig) {
			const char *baseName = basename(programName);
			configFileTemp = "/etc/";
			configFileTemp += baseName;
			configFileTemp += ".conf";
			Logger::Get() << "Trying to read configuration file: " << configFileTemp;
			//configFileTemp = "/etc/dispmanx_vncserver.conf";
			readConfig = TryReadConfigFile(config, configFileTemp);
		}
	}

	if (readConfig) {
		Logger::Get() << "Read config file: " << configFileTemp;

		config.lookupValue("relative", configData.relative);
		config.lookupValue("unsafe", configData.unsafe);
		config.lookupValue("downscale", configData.downscale);
		config.lookupValue("fullscreen", configData.fullscreen);
		config.lookupValue("localhost", configData.localhost);
		config.lookupValue("inetd", configData.inetd);
		config.lookupValue("multi-threaded", configData.multiThreaded);
		config.lookupValue("password", configData.password);
		config.lookupValue("port", configData.port);
		config.lookupValue("screen", configData.screen);
		config.lookupValue("frame-rate", configData.frameRate);
		config.lookupValue("vnc-params", configData.vncParams);
	}
	else
		Logger::Get() << "No config file found";

	return readConfig;
}

bool TryReadConfigFile(libconfig::Config& config, const std::string& file)
{
	try{
		config.readFile(file.c_str());
		return true;
	}
	catch (libconfig::FileIOException&) {
		return false;;
	}
	catch (libconfig::ParseException &e) {
		std::stringstream ss;
		ss << "Error: " << e.getError() << " on line " << e.getLine() << " while reading configuration file: " << file;
		throw Exception(ss.str());
	}
}

void usage(const char *programName)
{
	std::cerr << 
		"Usage: " << programName << " [OPTION]...\n"
		"\n"
		"  -a, --absolute               absolute mouse movements\n"
		"  -c, --config-file=FILE       use the specified configuration file\n"
		"  -d, --downscale              downscales the screen to a quarter in vnc\n"
		"  -f, --fullscreen             always runs fullscreen mode\n"
		"  -i, --inetd                  stdio instead of listening socket\n"
		"  -l, --localhost              only listens to local ports\n"
		"  -m, --multi-threaded         runs vnc in a separate thread\n"
		"  -p, --port=PORT              makes vnc available on the speficied port\n"
		"  -P, --password=PASSWORD      protects the session with PASSWORD\n"
		"  -r, --relative               relative mouse movements\n"
		"  -s, --screen=SCREEN          opens the specified screen number\n"
		"  -t, --frame-rate=RATE        sets the target frame rate, default is 15\n"
		"  -u, --unsafe                 disables more robust handling of resolution\n"
		"                               change at a small performance gain\n"
		"  -v, --vnc-params             parameters to send to libvncserver\n"
		"      --help                   displays this help and exit\n";
}
