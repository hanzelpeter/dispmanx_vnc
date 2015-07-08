#include "DMXDisplay.hh"

#include <stdio.h>

#include "Exception.hh"

DMXDisplay::~DMXDisplay()
{
	Close();
};

void DMXDisplay::Open(int screen)
{
	printf("Open display[%i]...\n", screen);
	m_display = vc_dispmanx_display_open(screen);
	if (m_display == DISPMANX_NO_HANDLE)
		throw Exception("vc_dispmanx_display_open failed");
}

void DMXDisplay::Close()
{
	if (m_display != DISPMANX_NO_HANDLE)
	{
		vc_dispmanx_display_close(m_display);
		m_display = DISPMANX_NO_HANDLE;
	}
}

bool DMXDisplay::IsOpen()
{
	return m_display != DISPMANX_NO_HANDLE;
}

void DMXDisplay::GetInfo(DISPMANX_MODEINFO_T& info)
{
	int ret = vc_dispmanx_display_get_info(m_display, &info);
	if (ret != 0)
		throw Exception("vc_dispmanx_display_get_info failed");
}

void DMXDisplay::Snapshot(DMXResource& resource, DISPMANX_TRANSFORM_T transform)
{
	int ret = vc_dispmanx_snapshot(m_display, resource.GetResourceHandle(), transform);
	if (ret != 0)
		throw Exception("vc_dispmanx_snapshot failed");
}
