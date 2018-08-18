#pragma once

#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <wordexp.h>

#include <rfb/rfb.h>
#undef min
#undef max

#include "BCMHost.hpp"
#include "DMXDisplay.hpp"
#include "DMXKeyboard.hpp"
#include "DMXMouse.hpp"
#include "DMXResource.hpp"
#include "UFile.hpp"

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

class FPSCounter
{
public:
	int GetFPS() const
	{
		return m_fps;
	}
	
	void Frame()
	{
		m_frameCount++;
		
		auto now = std::chrono::steady_clock::now();
		if((now - m_last) >= std::chrono::seconds(1))
		{
			m_fps = m_frameCount;
			m_frameCount = 0;
			m_last = now;
		}
	}

private:
	std::chrono::steady_clock::time_point m_last{};
	int m_frameCount{};
	int m_fps{};
};

struct Rect
{
	int top;
	int left;
	int bottom;
	int right;
};

class DMXVNCServer
{
public:
	DMXVNCServer(int frameRate);
	~DMXVNCServer();
	void Run(int port, const std::string& password,
				int screen, bool relativeMode, bool safeMode,
				bool bandwidthMode, bool multiThreaded, bool downscale,
				bool localhost,
				const std::string& vncParams);

private:
	void Open();
	void Close();
	bool IsOpen();
	std::chrono::steady_clock::duration TimeToTakePicture();
	bool TakePicture();
	static void DoPtr(int buttonMask, int x, int y, rfbClientPtr cl);
	static void DoKey(rfbBool down, rfbKeySym key, rfbClientPtr cl);
	static enum rfbNewClientAction newclient(rfbClientPtr cl);
	enum rfbNewClientAction NewClient(rfbClientPtr cl);
	static void clientgone(rfbClientPtr cl);
	void ClientGone(rfbClientPtr cl);

	const int BPP{2};
	const VC_IMAGE_TYPE_T imageType{VC_IMAGE_RGB565};
	
	BCMHost m_bcmHost;
	DMXDisplay m_display;
	DMXResource m_resource;
	DMXKeyboard m_keyboard;
	DMXMouse m_mouse;
	rfbScreenInfoPtr m_server{};
	
	std::chrono::steady_clock::duration m_idlePictureTimeout{std::chrono::milliseconds(500)};
	std::chrono::steady_clock::duration m_targetPictureTimeout{};
	std::chrono::steady_clock::duration m_pictureTimeout{};
	std::chrono::steady_clock::time_point m_timeLastFrameStart{};
	std::chrono::steady_clock::time_point m_timeLastFrameChange{};

	std::string m_desktopName;
	const char *m_passwords[2]{};
	std::string m_password;
	int m_clientCount{};

	FPSCounter m_fpsCounter;
	BandwidthController m_bandwidthController;

	std::vector<char> m_frameBuffer;
	int m_frameBufferPitch{};
	int m_frameBufferPaddedWidth{};

	ImageMap m_imageMap;
	std::vector<char> m_imageBuffer1;
	std::vector<char> m_imageBuffer2;
	void *m_image{};
	void *m_back_image{};

	DISPMANX_MODEINFO_T m_modeInfo{};
	bool m_safeMode{false};
	bool m_bandwidthMode{false};
	bool m_multiThreaded{false};
	bool m_downscale{false};
	bool m_toggleDownscale{false};
	int m_screen{0};

	std::string m_lastPrintedMessage;

	int m_padded_width{};
	int m_pitch{};
	Rect m_frameRect{};

	uint32_t  m_vc_image_ptr{};
	
	wordexp_t m_we{};
};
