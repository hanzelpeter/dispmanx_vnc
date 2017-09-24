## dispmanx_vnc

VNC Server for Raspberry Pi using dispmanx

### Dependencies

* `libvncserver-dev`
* `raspberrypi-firmware` (some distros will call this `rbp-userland-dev-osmc`)

### Build

Compile with ./makeit or use make

### Packages
* ![logo](https://s19.postimg.org/b2hf0wbar/64x64.png "arch logo")Arch ARM: in the [AUR](https://aur.archlinux.org/packages/dispmanx_vnc).

### Notes
If you want to use X, modprobe evdev first.
Use -r for relative mode
Use -a for absolute mode
Without arguments it uses absolute mode for mouse

Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.
