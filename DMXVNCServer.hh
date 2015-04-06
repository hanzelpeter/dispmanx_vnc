#ifndef DMXVNCSERVER_HH
#define DMXVNCSERVER_HH

#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <algorithm>

#include <rfb/rfb.h>
#include <rfb/keysym.h>

#include "BCMHost.hh"
#include "DMXDisplay.hh"
#include "DMXResource.hh"
#include "UFile.hh"

class ImageMap
{
public:
	void Resize(int height, int width);
	void Clear();
	int GetChangedRegionRatio();

	int pixelsPerRegion = 50;
	int height = 0;
	int width = 0;
	int mapWidth = 0;
	int mapHeight = 0;
	int changedRegionRatio = -1;
	std::vector<bool> imageMap;
};

class BandwidthController
{
public:
	void ControlMode(int changedRegionRatio);

	int smallUpdateFramesSize = 10;
	int largeUpdateFramesSize = 30;

	int smallUpdateFramesSwitchCount = 5;
	int largeUpdateFramesSwitchCount = 5;

	int largeUpdateFrames = 0;
	int smallUpdateFrames = 0;
	bool largeFrameMode = false;
};

class DMXVNCServer
{
public:
	DMXVNCServer(int BPP, int frameRate);
	~DMXVNCServer();
	void Open();
	void Close();
	bool IsOpen();
	void Run(int argc, char *argv[], int port, const std::string& password,
				int screen, bool relativeMode, bool safeMode,
				bool bandwidthMode, bool multiThreaded);
	double TimeToTakePicture();
	int TakePicture(unsigned char *buffer);
	int keysym2scancode(rfbKeySym key);
	static void doptr(int buttonMask, int x, int y, rfbClientPtr cl);
	void DoPtr(int buttonMask, int x, int y, rfbClientPtr cl);
	static void dokey(rfbBool down, rfbKeySym key, rfbClientPtr cl);
	void DoKey(rfbBool down, rfbKeySym key, rfbClientPtr cl);
	static enum rfbNewClientAction newclient(rfbClientPtr cl);
	enum rfbNewClientAction NewClient(rfbClientPtr cl);
	static void clientgone(rfbClientPtr cl);
	void ClientGone(rfbClientPtr cl);

private:
	BCMHost m_bcmHost;
	DMXDisplay m_display;
	DMXResource m_resource;
	UFile m_ufile;
	rfbScreenInfoPtr server = nullptr;
	int BPP = 0;
	float idlePictureTimeout = 0.5;
	float targetPictureTimeout = 0.0;
	float pictureTimeout = 0.0;

	std::string desktopName;
	const char *passwords[2] = { nullptr, nullptr };
	std::string password;
	int clients = 0;

	BandwidthController bandwidthController;

	ImageMap imageMap;
	std::vector<char> frameBuffer;
	std::vector<char> imageBuffer1;
	std::vector<char> imageBuffer2;
	void *image = nullptr;
	void *back_image = nullptr;

	DISPMANX_MODEINFO_T info = { 0 };
	bool relativeMode = false;
	bool safeMode = false;
	bool bandwidthMode = false;
	bool multiThreaded = false;
	int screen = 0;

	double timeLastFrameStart = 0.0;
	double timeLastFrameChange = 0.0;
	std::string lastPrintedMessage;

	int padded_width = 0;
	int pitch = 0;
	int r_x0 = 0;
	int r_y0 = 0;
	int r_x1 = 0;
	int r_y1 = 0;

	int mouse_last = 0;
	int last_x = 0;
	int last_y = 0;

	VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;
	uint32_t  vc_image_ptr = 0;
};

#endif // DMXVNCSERVER_HH
