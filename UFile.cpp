#include "UFile.hh"

#include "Exception.hh"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

UFile::~UFile()
{
	Close();
};

void UFile::Open(bool relativeMode, int width, int height)
{
	ufile = open("/dev/uinput", O_WRONLY | O_NDELAY);
	printf("open /dev/uinput returned %d.\n", ufile);
	if (ufile == 0) {
		throw Exception("Could not open uinput.\n");
	}

	struct uinput_user_dev uinp{};
	uinp.id.vendor  = 1;
	uinp.id.product = 1;
	uinp.id.version = 1;

	if(width) {
		m_name = "VNCServer Mouse";

		ioctl(ufile, UI_SET_EVBIT, EV_KEY);

		if (!relativeMode) {
			uinp.absmin[ABS_X] = 0;
			uinp.absmax[ABS_X] = width;
			uinp.absmin[ABS_Y] = 0;
			uinp.absmax[ABS_Y] = height;
		}

	        ioctl(ufile, UI_SET_KEYBIT, BTN_LEFT);
	        ioctl(ufile, UI_SET_KEYBIT, BTN_RIGHT);
	        ioctl(ufile, UI_SET_KEYBIT, BTN_MIDDLE);

	        if (relativeMode) {
	                ioctl(ufile, UI_SET_EVBIT, EV_REL);
	                ioctl(ufile, UI_SET_RELBIT, REL_X);
	                ioctl(ufile, UI_SET_RELBIT, REL_Y);
	        }
	        else {
	                ioctl(ufile, UI_SET_EVBIT, EV_ABS);
                	ioctl(ufile, UI_SET_ABSBIT, ABS_X);
        	        ioctl(ufile, UI_SET_ABSBIT, ABS_Y);
	        }
	}
	else {
		m_name = "VNCServer Keyboard";

		ioctl(ufile, UI_SET_EVBIT, EV_KEY);
		ioctl(ufile, UI_SET_EVBIT, EV_REP);
		ioctl(ufile, UI_SET_EVBIT, EV_SYN);

		for (int i = 0; i<KEY_MAX; i++) {
			ioctl(ufile, UI_SET_KEYBIT, i);
		}
	}

	strncpy(uinp.name, m_name.c_str(), UINPUT_MAX_NAME_SIZE);

	int retcode;
	retcode = write(ufile, &uinp, sizeof(uinp));
	printf("First write returned %d.\n", retcode);

	retcode = ioctl(ufile, UI_DEV_CREATE);
	printf("ioctl UI_DEV_CREATE returned %d.\n", retcode);
	if (retcode) {
		throw Exception("Error create uinput device %d.\n");
	}
}

void UFile::Close()
{
	if (ufile != -1)
	{
		ioctl(ufile, UI_DEV_DESTROY);
		close(ufile);
		ufile = -1;
	}
}

void UFile::WriteEvent(__u16 type, __u16 code, __s32 value)
{
	struct input_event inputEvent = { { 0 } };
	gettimeofday(&inputEvent.time, NULL);
	inputEvent.type = type;
	inputEvent.code = code;
	inputEvent.value = value;
	if(-1 == write(ufile, &inputEvent, sizeof(inputEvent)))
	{
		std::cout << "Error " << errno << " writing to " << m_name << '\n';
	}
};
