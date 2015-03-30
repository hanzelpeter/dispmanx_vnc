dispmanx_vnc
============

VNC Server for Raspberry PI using dispmanx

Compile with "make"

you need to have installed libvncserver-dev and gcc/g++-4.7. 

On raspbian you prepare using the following steps.

	apt-get install gcc-4.7 g++-4.7 libvncserver-dev

Followed by 
	sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.6 60 --slave /usr/bin/g++ g++ /usr/bin/g++-4.6 
	sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.7 40 --slave /usr/bin/g++ g++ /usr/bin/g++-4.7 

And finally select the version you want to use with the following command
	sudo update-alternatives --config gcc


If you want to use X, modprobe evdev first.
Use -r for relative mode
Use -a for absolute mode
Without arguments it uses absolute mode for mouse

Relative mode makes hello_triangle2 to work. And also mouse moving in minecraft is better.
Still the mouse is tricky.
