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
If you want to use X, modprobe evdev first.
Use -r for relative mode
Use -a for absolute mode
Without arguments it uses absolute mode for mouse

Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.

### Usage
dispmanx_vnc uses libvncserver-dev, so it takes all the arguments vncserver does.  See "./dispmanx_vnc -help" for possible options.
You may already have a vncserver running; In which case use dispmanx_vnc with another port than the default 5900. For example:
sudo ./dispmanx_vnc rfbport 5901


### Possible Errors
If you see the message, "open /dev/uinput returned -1." it is because you are trying to run dispmanx without being root.
