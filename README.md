dispmanx_vnc
============

VNC Server for Raspberry PI using dispmanx

Build with "make"

Building requires that the following is installed: libvncserver-dev, libconfig++-dev and g++ 4.7 or newer. 

Features
--------
- Practically no CPU usage when a VNC client is not connected. No need to stop when not in use.
- Runs well as a service
- Stable even when changing resolution
- Password support
- Configurable port number
- Inetd support
- Supports listening for new clients or terminating server when a client disconnects
- Configurable target frame rate, with automatic frame rate reduction down to 2/second when no updates are detected.
- Adaptable algorithm for the best performance both for update intensive applications like video and animations, and low CPU usage for updates to smaller regions of the screen
- Supports efficient downscaling to a quarter of the resoltion on the server side. Saves CPU, bandwidth and is practical for remote control of e.g. Kodi in high resolution from a computer with lower resolution

On raspbian wheezy, prepare using the following steps
-----------------------------------------------------

	sudo apt-get install g++-4.8 libvncserver-dev libconfig++-dev

Followed by 

	sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.6 60 --slave /usr/bin/g++ g++ /usr/bin/g++-4.6 
	sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 40 --slave /usr/bin/g++ g++ /usr/bin/g++-4.8 

And finally select the version you want to use with the following command

	sudo update-alternatives --config gcc

...or simply build with

	make CXX=g++-4.8

On raspbian jessie or buster, prepare using the following
---------------------------------------------------------
	sudo apt-get install g++ libvncserver-dev libconfig++-dev

On OSMC, prepare using the following
------------------------------------
	sudo apt-get install build-essential rbp-userland-dev-osmc libvncserver-dev libconfig++-dev

Building a deb package on raspbian buster
-----------------------------------------
In addition to the above build requirements, install the build tools

	sudo apt-get install dh-exec bzr-builddeb build-essential

Run in the extracted source directory

	dpkg-buildpackage -b -rfakeroot -us -uc

If the linker complains about missing GLESv2 or EGL, that is due to missing libraries under /opt/vc/lib. It may be corrected using

	for l in EGL GLESv2; do [ -e /opt/vc/lib/lib${l}.so ] || ([ -f /opt/vc/lib/libbrcm${l}.so ] && ln -s libbrcm${l}.so /opt/vc/lib/lib${l}.so); done

The deb packages will be built one directory above the source. Disregard the *dbgsym* package.

To install the package

	sudo dpkg -i ../dispmanx-vnc_*_armhf.deb

Running dispmanx_vnc from dispmanx_vnc package
----------------------------------------------
There is no need to run any services. By default, the package runs a systemd-based socket listener on port TCP 5901. The socket will be listening on boot as well.

The systemd listener may be queried with

	systemctl status dispmanx_vnc.socket

Among other things, the status command will display the listening port(s), the count of currently connected clients and the tally of clients connected so far.

Note no dispmanx_vnc processes are run until at least one client is connected. This ensures very lightweight resource use, suitable for low memory environments, such as Raspberry PI Zero.

For every connected client, a new dispmanx_vnc instance is started. When a client quits, the service instance and the process spawned by the socket are stopped.

The clients connected may be viewed with

	systemctl | grep dispmanx_vnc@

Changing listener port in dispmanx-vnc package
----------------------------------------------
When a systemd socket listens on behalf of dispmanx-vnc, the listening port is defined in a systemd configuration file.

The default listening port may be changed with a systemd drop-in file.

For example, the following will set the port to 5910 & restart the listener

	sudo mkdir -p /etc/systemd/system/dispmanx_vnc.socket.d
	echo -e '[Socket]\nListenStream=\nListenStream=5910' | sudo tee /etc/systemd/system/dispmanx_vnc.socket.d/00_port_override.conf
	sudo systemctl daemon-reload
	sudo systemctl restart dispmanx_vnc.socket

To revert to the default listening port, the overide file needs to be removed & the socket restarted

	rm /etc/systemd/system/dispmanx_vnc.socket.d/00_port_override.conf
	sudo systemctl daemon-reload
	sudo systemctl restart dispmanx_vnc.socket

If the keyboard or mouse does not work
--------------------------------------
Make sure the appropriate driver is loaded by issuing

	sudo modprobe evdev

Make this load automatically at boot by adding the following on a separate row in /etc/modules

	evdev

After build
-----------
Run the program by issuing

	sudo ./dispmanx_vncserver
	
Parameters
	Usage: ./dispmanx_vncserver [OPTION]...

	-a, --absolute               absolute mouse movements
	-c, --config-file=FILE       use the specified configuration file
	-d, --downscale              downscales the screen to a quarter in vnc
	-f, --fullscreen             always runs fullscreen mode
	-i, --inetd                  stdio instead of listening socket
	-l, --localhost              only listens to local ports
	-m, --multi-threaded         runs vnc in a separate thread
	-o, --once                   connect once, then terminate
	-p, --port=PORT              makes vnc available on the speficied port
	-P, --password=PASSWORD      protects the session with PASSWORD
	-r, --relative               relative mouse movements
	-s, --screen=SCREEN          opens the specified screen number
	-t, --frame-rate=RATE        sets the target frame rate, default is 15
	-u, --unsafe                 disables more robust handling of resolution
	                             change at a small performance gain
	-v, --vnc-params             parameters to send to libvncserver
	    --help                   displays this help and exit

Config file
-----------
The program supports reading all the settings from a configuration file. See the attached .conf.sample-file. The default name of the config file is the same as of the binary with the extension ".conf". The program will first look in the same folder as the binary is placed, if not found there it will try /etc/. The configuration file name and location may be specified with the --config-file command line parameter. Any command line arguments will override those of the config file.



Screen resolution / Headless operation
--------------------------------------
By default, when HDMI not connected, the composite port is active with a resolution of 720x480.
Edit the /boot/config.txt file to change resolution.

	sudo nano /boot/config.txt

Add or replace the following settings to enable 1080p 50Hz suitable for a TV set

	# This enables HDMI resolutions when HDMI is not connected
	hdmi_force_hotplug=1

	# Group
	# 1=CEA, used for connecting to consumer TV sets
	# 2=DMT, used for connecting to a computer monitor
	hdmi_group=1

	# CEA Modes
	#  4 =  720p     60Hz
	# 19 =  720p     50Hz
	# 16 = 1080p     60Hz
	# 31 = 1080p     50Hz
	# DMT Modes
	# 39 = 1360x768    60Hz
	# 82 = 1920x1080   60Hz
	hdmi_mode=31

The modes specified here are just a few useful examples, please refer to the documentation of config.txt at raspberrypi.org for further information
https://www.raspberrypi.org/documentation/configuration/config-txt.md

If you occasionally connect to a monitor with HDMI, it is an advantage to specify hdmi_group and hdmi_mode that matches the monitor.

A reboot is required after changing this file for the changes to take effect.

Other
-----
Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.
