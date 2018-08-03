#include "DMXResource.hpp"
#include "Exception.hpp"

DMXResource::~DMXResource()
{
	Close();
};

void DMXResource::Create(VC_IMAGE_TYPE_T type, int width, int height, uint32_t *vc_image_ptr)
{
	m_resource = vc_dispmanx_resource_create(type, width, height, vc_image_ptr);
	if (m_resource == DISPMANX_NO_HANDLE)
		throw Exception("vc_dispmanx_resource_create failed");
}

void DMXResource::Close()
{
	if (m_resource != DISPMANX_NO_HANDLE) {
		vc_dispmanx_resource_delete(m_resource);
		m_resource = DISPMANX_NO_HANDLE;
	}
}

void DMXResource::ReadData(VC_RECT_T& rect, void *image, int pitch)
{
	vc_dispmanx_resource_read_data(m_resource, &rect, image, pitch);
}

DISPMANX_RESOURCE_HANDLE_T DMXResource::GetResourceHandle()
{
	return m_resource;
}

