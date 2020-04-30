## dispmanx_vnc

VNC Server for Raspberry Pi using dispmanx

### Dependencies

* `libvncserver-dev`
* `raspberrypi-firmware` (some distros will call this `rbp-userland-dev-osmc`)

### Build

Compile with ./makeit or use make

### Packages
* ![logo](http://www.monitorix.org/imgs/archlinux.png "arch logo")Arch ARM: in the [AUR](https://aur.archlinux.org/packages/dispmanx_vnc).

### Notes
If you want to use X, modprobe evdev first.\
Use -h for this small help
Use -help for VNC server options help
Use -r for relative mode\
Use -a for absolute mode\
Use -d X for dispmanx ID (default is 0)\
&nbsp;&nbsp;&nbsp;&nbsp;according some docs:\
&nbsp;&nbsp;&nbsp;&nbsp;0 - DSI/DPI LCD (I use 0 for headless RPI and also 3 works)\
&nbsp;&nbsp;&nbsp;&nbsp;2 - HDMI 0\
&nbsp;&nbsp;&nbsp;&nbsp;3 - SDTV\
&nbsp;&nbsp;&nbsp;&nbsp;7 - HDMI 1\
Without arguments it starts with display ID = 0 with absolute mouse mode

Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.

### Usage
dispmanx_vnc uses libvncserver-dev, so it takes all the arguments vncserver does.  See "./dispmanx_vnc -help" for possible options.
You may already have a vncserver running; In which case use dispmanx_vnc with another port than the default 5900. For example:
sudo ./dispmanx_vnc rfbport 5901


### Possible Errors
If you see the message, "open /dev/uinput returned -1." it is because you are trying to run dispmanx without being root.
