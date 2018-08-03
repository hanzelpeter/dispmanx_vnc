#ifndef DMXDISPLAY_HH
#define DMXDISPLAY_HH

#include "bcm_host.h"

#include "DMXResource.hpp"

class DMXDisplay
{
public:
	~DMXDisplay();
	void Open(int screen);
	void Close();
	bool IsOpen();
	void GetInfo(DISPMANX_MODEINFO_T& info);
	void Snapshot(DMXResource& resource, DISPMANX_TRANSFORM_T transform);

private:
	DISPMANX_DISPLAY_HANDLE_T m_display = DISPMANX_NO_HANDLE;
};

#endif // DMXDISPLAY_HH
