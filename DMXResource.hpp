#pragma once

#include "bcm_host.h"

class DMXResource
{
public:
	~DMXResource();
	void Create(VC_IMAGE_TYPE_T type, int width, int height, uint32_t *vc_image_ptr);
	void Close();
	void ReadData(VC_RECT_T& rect, void *image, int pitch);
	DISPMANX_RESOURCE_HANDLE_T GetResourceHandle();

private:
	DISPMANX_RESOURCE_HANDLE_T  m_resource = DISPMANX_NO_HANDLE;
};
