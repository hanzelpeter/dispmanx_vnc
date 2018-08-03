#include "UFile.hpp"

#include "Exception.hpp"
#include "Logger.hpp"

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
	m_ufile = open("/dev/uinput", O_WRONLY | O_NDELAY);
	Logger::Get() << "open /dev/uinput returned " << m_ufile;
	if (m_ufile == 0) {
		throw Exception("Could not open uinput");
	}

	struct uinput_user_dev uinp{};
	uinp.id.vendor  = 1;
	uinp.id.product = 1;
	uinp.id.version = 1;

	if(width) {
		m_name = "VNCServer Mouse";

		ioctl(m_ufile, UI_SET_EVBIT, EV_KEY);

		if (!relativeMode) {
			uinp.absmin[ABS_X] = 0;
			uinp.absmax[ABS_X] = width;
			uinp.absmin[ABS_Y] = 0;
			uinp.absmax[ABS_Y] = height;
		}

	        ioctl(m_ufile, UI_SET_KEYBIT, BTN_LEFT);
	        ioctl(m_ufile, UI_SET_KEYBIT, BTN_RIGHT);
	        ioctl(m_ufile, UI_SET_KEYBIT, BTN_MIDDLE);

	        if (relativeMode) {
	                ioctl(m_ufile, UI_SET_EVBIT, EV_REL);
	                ioctl(m_ufile, UI_SET_RELBIT, REL_X);
	                ioctl(m_ufile, UI_SET_RELBIT, REL_Y);
	        }
	        else {
	                ioctl(m_ufile, UI_SET_EVBIT, EV_ABS);
                	ioctl(m_ufile, UI_SET_ABSBIT, ABS_X);
        	        ioctl(m_ufile, UI_SET_ABSBIT, ABS_Y);
	        }
	}
	else {
		m_name = "VNCServer Keyboard";

		ioctl(m_ufile, UI_SET_EVBIT, EV_KEY);
		ioctl(m_ufile, UI_SET_EVBIT, EV_REP);
		ioctl(m_ufile, UI_SET_EVBIT, EV_SYN);

		for (int i = 0; i<KEY_MAX; i++) {
			ioctl(m_ufile, UI_SET_KEYBIT, i);
		}
	}

	strncpy(uinp.name, m_name.c_str(), UINPUT_MAX_NAME_SIZE);

	int retcode;
	retcode = write(m_ufile, &uinp, sizeof(uinp));
	Logger::Get() << "First write returned " << retcode;

	retcode = ioctl(m_ufile, UI_DEV_CREATE);
	Logger::Get() << "ioctl UI_DEV_CREATE returned " << retcode;
	if (retcode) {
		throw Exception("Error creating uinput device: " + std::to_string(retcode));
	}
}

void UFile::Close()
{
	if (m_ufile != -1)
	{
		ioctl(m_ufile, UI_DEV_DESTROY);
		close(m_ufile);
		m_ufile = -1;
	}
}

void UFile::WriteEvent(__u16 type, __u16 code, __s32 value)
{
	struct input_event inputEvent = { { 0 } };
	gettimeofday(&inputEvent.time, NULL);
	inputEvent.type = type;
	inputEvent.code = code;
	inputEvent.value = value;
	if(-1 == write(m_ufile, &inputEvent, sizeof(inputEvent)))
	{
		Logger::Get() << "Error " << errno << " writing to " << m_name;
	}
};
