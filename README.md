dispmanx_vnc
============

VNC Server for Raspberry PI using dispmanx

compile with ./makeit

you need to have installed libvncserver-dev

If you want to use X, modprobe evdev first.
Use -r for relative mode
Use -a for absolute mode
Without arguments it uses absolute mode for mouse

Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.
