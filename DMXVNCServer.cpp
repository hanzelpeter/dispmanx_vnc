#include "DMXVNCServer.hpp"

#include "Exception.hpp"
#include "Logger.hpp"

#include <thread>
#include <mutex>
#include <algorithm>

bool terminate{false};

namespace
{
	char localhost_address[] = "localhost";
	
	template<typename T>
	auto AlignUp(T value, T alignment) -> T
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	static void LogVNCServer(const char *format, ...)
	{
		va_list args;
		va_start(args, format);

		char buffer[1024];
		auto count = vsprintf(buffer, format, args);
		if(buffer[count - 1] == '\n') {
			buffer[count - 1] = '\0';
		}
		Logger::Get("libvnc") << buffer;

		va_end(args);
	}
}

DMXVNCServer::DMXVNCServer(int frameRate)
{
	m_targetPictureTimeout = std::chrono::milliseconds(1000 / frameRate);
	
	rfbLog = LogVNCServer;
	rfbErr = LogVNCServer;
}

DMXVNCServer::~DMXVNCServer()
{
	Close();
	if (m_server) {
		rfbShutdownServer(m_server, FALSE);
		rfbScreenCleanup(m_server);
		m_server = nullptr;
	};

	if(m_we.we_offs) {
		wordfree(&m_we);
	}
}

void DMXVNCServer::Open()
{
	m_display.Open(m_screen);
	m_display.GetInfo(m_modeInfo);

	Logger::Get() << "ModeInfo: " << m_modeInfo.width << ", " << m_modeInfo.height << ", " 
		<< m_modeInfo.transform << ", " << m_modeInfo.input_format;

	/* DispmanX expects buffer rows to be aligned to a 32 bit boundarys */
	m_pitch = AlignUp(BPP * m_modeInfo.width, 32);
	m_padded_width = m_pitch / BPP;

	if (m_downscale) {
		m_frameBufferPitch = AlignUp(BPP * m_modeInfo.width / 2, 32);
		m_frameBufferPaddedWidth = m_frameBufferPitch / BPP;
	}
	else {
		m_frameBufferPitch = m_pitch;
		m_frameBufferPaddedWidth = m_padded_width;
	}

	m_imageBuffer1.resize(m_pitch * m_modeInfo.height);
	std::fill(m_imageBuffer1.begin(), m_imageBuffer1.end(), '\0');
	m_imageBuffer2.resize(m_pitch * m_modeInfo.height);
	std::fill(m_imageBuffer2.begin(), m_imageBuffer2.end(), '\0');
	m_image = &m_imageBuffer1[0];
	m_back_image = &m_imageBuffer2[0];

	m_resource.Create(imageType, m_modeInfo.width, m_modeInfo.height, &m_vc_image_ptr);

	m_mouse.SetLastX(m_padded_width / 2);
	m_mouse.SetLastY(m_modeInfo.height / 2);
}

void DMXVNCServer::Close()
{
	m_resource.Close();
	m_display.Close();
	m_imageBuffer1.resize(0);
	m_imageBuffer2.resize(0);
	m_image = nullptr;
	m_back_image = nullptr;
}

bool DMXVNCServer::IsOpen()
{
	return m_display.IsOpen();
}

void DMXVNCServer::Run(int port, const std::string& password,
						int screen, bool relativeMode, bool safeMode,
						bool bandwidthMode, bool multiThreaded, bool downscale,
						bool localhost,
						const std::string& vncParams)
{
	m_safeMode = safeMode;
	m_bandwidthMode = bandwidthMode;
	m_screen = screen;
	m_multiThreaded = multiThreaded;
	m_downscale = downscale;

	Open();

	m_we.we_offs = 1;
	wordexp(vncParams.c_str(), &m_we, WRDE_DOOFFS);
	int argc = m_we.we_offs + m_we.we_wordc;
	std::vector<char *> argv(argc);

	for(int i = 0; i < argc; i++) {
		argv[i] = m_we.we_wordv[i];
	}	

	if (m_downscale) {
		m_server = rfbGetScreen(&argc, &argv[0], m_frameBufferPaddedWidth, m_modeInfo.height / 2, 5, 3, BPP);
	}
	else {
		m_server = rfbGetScreen(&argc, &argv[0], m_frameBufferPaddedWidth, m_modeInfo.height, 5, 3, BPP);
	}
	
	if (!m_server)
		throw Exception("rfbGetScreen failed");

	if (port){
		m_server->port = port;
	}
	
	if(localhost) {
		m_server->listen6Interface = localhost_address;
		rfbStringToAddr(localhost_address, &m_server->listenInterface);
	}

	if (password.length()) {
		m_password = password;
		m_passwords[0] = m_password.c_str();
		m_server->authPasswdData = (void *)m_passwords;
		m_server->passwordCheck = rfbCheckPasswordByList;
	}

	char hostname[HOST_NAME_MAX + 1];
	if (0 == gethostname(hostname, sizeof(hostname))){
		m_desktopName = hostname;
		m_desktopName += " : ";
		m_desktopName += std::to_string(m_screen);
		m_server->desktopName = m_desktopName.c_str();
	}
	else
		m_server->desktopName = "VNC server via dispmanx";

	if (m_downscale)
		m_frameBuffer.resize(m_frameBufferPitch*m_modeInfo.height / 2);
	else
		m_frameBuffer.resize(m_frameBufferPitch*m_modeInfo.height);
	m_server->frameBuffer = (char*)&m_frameBuffer[0];
	m_server->alwaysShared = (1 == 1);
	m_server->kbdAddEvent = DoKey;
	m_server->ptrAddEvent = DoPtr;
	m_server->newClientHook = newclient;
	m_server->screenData = this;

	/*
	m_server->serverFormat.redShift = 11;
	m_server->serverFormat.blueShift = 0;
	m_server->serverFormat.greenShift = 6;
	*/
	
	Logger::Get() << "Server bpp: " << static_cast<int>(m_server->serverFormat.bitsPerPixel) << '\n'
		<< "\tServer bigEndian: " << static_cast<int>(m_server->serverFormat.bigEndian) << '\n'
		<< "\tServer redShift: " << static_cast<int>(m_server->serverFormat.redShift) << '\n'
		<< "\tServer blueShift: " << static_cast<int>(m_server->serverFormat.blueShift) << '\n'
		<< "\tServer greeShift: " << static_cast<int>(m_server->serverFormat.greenShift);

	/* Initialize the server */
	rfbInitServer(m_server);
	if (m_multiThreaded)
		rfbRunEventLoop(m_server,-1,TRUE);

	m_mouse.Open(relativeMode, m_modeInfo.width, m_modeInfo.height);
	m_keyboard.Open();

	if (m_bandwidthMode)
		m_imageMap.Resize(m_modeInfo.height, m_modeInfo.width);

	/* Loop, processing clients and taking pictures */
	int errors = 0;
	bool toggledDownscale = false;
	while (!terminate && rfbIsActive(m_server)) {
		std::chrono::steady_clock::duration timeToTakePicture{};
		if (m_clientCount && 0 == (timeToTakePicture = TimeToTakePicture()).count()) {
			try {
				if(m_toggleDownscale)
				{
					Close();
					m_downscale = !m_downscale;
					m_toggleDownscale = false;
					toggledDownscale = true;
				}
				if (!IsOpen())
				{
					Open();
					if (toggledDownscale ||	m_modeInfo.width != m_server->width || m_modeInfo.height != m_server->height) {
						if (m_downscale) {
							m_frameBuffer.resize(m_frameBufferPitch*m_modeInfo.height / 2);
							std::fill(m_frameBuffer.begin(), m_frameBuffer.end(), '\0');
							rfbNewFramebuffer(m_server, &m_frameBuffer[0], m_frameBufferPaddedWidth, m_modeInfo.height / 2, 5, 3, BPP);
						}
						else {
							m_frameBuffer.resize(m_frameBufferPitch*m_modeInfo.height);
							std::fill(m_frameBuffer.begin(), m_frameBuffer.end(), '\0');
							rfbNewFramebuffer(m_server, &m_frameBuffer[0], m_frameBufferPaddedWidth, m_modeInfo.height, 5, 3, BPP);
						}
						m_imageMap.Resize(m_modeInfo.height, m_modeInfo.width);
						toggledDownscale = false;
					}
				}

				if (TakePicture()) {
					m_timeLastFrameChange = std::chrono::steady_clock::now();
					m_pictureTimeout = m_targetPictureTimeout;
					if (m_bandwidthMode && !m_bandwidthController.largeFrameMode && m_imageMap.GetChangedRegionRatio() < m_bandwidthController.largeUpdateFramesSize) {
						for (int y = 0; y < m_imageMap.mapHeight; y++){
							for (int x = 0; x < m_imageMap.mapWidth; x++){
								if (m_imageMap.imageMap[(y * m_imageMap.mapWidth) + x]) {
									if (m_downscale){
										rfbMarkRectAsModified(m_server,
											std::max(m_frameRect.left / 2, x * m_imageMap.pixelsPerRegion / 2),
											std::max(m_frameRect.top / 2, y * m_imageMap.pixelsPerRegion / 2),
											std::min(m_frameRect.right / 2, (x * m_imageMap.pixelsPerRegion) / 2 + m_imageMap.pixelsPerRegion / 2),
											std::min(m_frameRect.bottom / 2, (y * m_imageMap.pixelsPerRegion) / 2 + m_imageMap.pixelsPerRegion / 2));
									}
									else{
										rfbMarkRectAsModified(m_server,
											std::max(m_frameRect.left, x * m_imageMap.pixelsPerRegion),
											std::max(m_frameRect.top, y * m_imageMap.pixelsPerRegion),
											std::min(m_frameRect.right, (x * m_imageMap.pixelsPerRegion) + m_imageMap.pixelsPerRegion),
											std::min(m_frameRect.bottom, (y * m_imageMap.pixelsPerRegion) + m_imageMap.pixelsPerRegion));
									}
								}
							}
						}
					}
					else
					{
						if (m_downscale) {
							rfbMarkRectAsModified(m_server, m_frameRect.left / 2, m_frameRect.top / 2, m_frameRect.right / 2, m_frameRect.bottom / 2);
						}
						else {
							rfbMarkRectAsModified(m_server, m_frameRect.left, m_frameRect.top, m_frameRect.right, m_frameRect.bottom);
						}
					}
				}
				else {
					if (m_targetPictureTimeout < m_idlePictureTimeout) {
						auto idleSleepFactor = std::max(std::chrono::seconds{1}.count(), std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_timeLastFrameChange).count());
						m_pictureTimeout = std::min(m_idlePictureTimeout, m_targetPictureTimeout * idleSleepFactor);
					}
				}

				if (m_bandwidthMode) {
					m_bandwidthController.ControlMode(m_imageMap.GetChangedRegionRatio());
				}

				errors = 0;
			}
			catch (Exception& e) {
				Logger::Get() << "Caught exception: " << e.what();
				errors++;
				if (errors > 10)
					throw e;
				Close();
			}
		}


		std::chrono::steady_clock::duration sleep_for;

		if (!m_clientCount) {
			sleep_for = std::chrono::milliseconds{100};
			if (IsOpen())
				Close();
		}
		else {
			sleep_for = std::max(timeToTakePicture, std::chrono::steady_clock::duration{std::chrono::milliseconds{m_server->deferUpdateTime}});
		}

		if (m_multiThreaded) {
			std::this_thread::sleep_for(sleep_for);
		}
		else {
			if( rfbProcessEvents(m_server, std::chrono::duration_cast<std::chrono::microseconds>(sleep_for).count()))
				m_pictureTimeout = m_targetPictureTimeout;
		}
	}
}

/*
* throttle camera updates
*/
std::chrono::steady_clock::duration DMXVNCServer::TimeToTakePicture() {
	auto now = std::chrono::steady_clock::now();
	std::chrono::steady_clock::duration elapsed = now - m_timeLastFrameStart;

	if (elapsed > m_pictureTimeout)
		m_timeLastFrameStart = now;
	return std::max(std::chrono::steady_clock::duration{}, m_pictureTimeout - elapsed);
};

/*
* simulate grabbing a picture from some device
*/
bool DMXVNCServer::TakePicture()
{
	DISPMANX_TRANSFORM_T transform{};
	VC_RECT_T			rect;

	if (m_safeMode)
	{
		DISPMANX_MODEINFO_T modeInfo;
		m_display.GetInfo(modeInfo);
		if (modeInfo.width != m_modeInfo.width || modeInfo.height != m_modeInfo.height)
			throw Exception("New mode detected");
	}

	m_display.Snapshot(m_resource, transform);

	vc_dispmanx_rect_set(&rect, 0, 0, m_modeInfo.width, m_modeInfo.height);
	m_resource.ReadData(rect, m_image, m_pitch);

	uint32_t* image_lp = (uint32_t*)m_image;
	uint32_t* buffer_lp = (uint32_t*)m_server->frameBuffer;
	uint32_t* back_image_lp = (uint32_t*)m_back_image;

	int lp_padding = m_padded_width >> 1;

	if (m_bandwidthMode){
		m_imageMap.Clear();
	}

	if (m_downscale) {
		uint16_t *buffer_16p = (uint16_t *)m_server->frameBuffer;

		m_frameRect.top = m_modeInfo.height;
		m_frameRect.bottom = -1;
		m_frameRect.left = m_modeInfo.width / 2;
		m_frameRect.right = -1;

		for (int y = 0; y < m_modeInfo.height; y += 2) {
			for (int x = 0; x < (m_modeInfo.width / 2); x++) {
				if (back_image_lp[y * lp_padding + x] != image_lp[y * lp_padding + x] ||
					back_image_lp[(y + 1) * lp_padding + x] != image_lp[(y + 1) * lp_padding + x]) {

					if (m_frameRect.top == m_modeInfo.height) {
						m_frameRect.top = m_frameRect.bottom = y;
						m_frameRect.left = m_frameRect.right = x;
					}
					else {
						if (y > m_frameRect.bottom) m_frameRect.bottom = y + 1;
						if (x < m_frameRect.left) m_frameRect.left = x;
						if (x > m_frameRect.right) m_frameRect.right = x;
					}

					{
						uint32_t tbi1 = image_lp[y * lp_padding + x];
						uint32_t tbi2 = image_lp[(y + 1) * lp_padding + x];

						uint16_t r1, r2, r3;
						r1 = ((tbi1 & 0b0000000000011111) +
							((tbi1 >> 16) & 0b0000000000011111) +
							(tbi2 & 0b0000000000011111) +
							((tbi2 >> 16) & 0b0000000000011111)) / 4;

						r2 = (((tbi1 & 0b0000011111000000) >> 6) +
							(((tbi1 >> 16) & 0b0000011111000000) >> 6) +
							((tbi2 & 0b0000011111000000) >> 6) +
							(((tbi2 >> 16) & 0b0000011111000000) >> 6)) / 4;

						r3 = (((tbi1 & 0b1111100000000000) >> 11) +
							(((tbi1 >> 16) & 0b1111100000000000) >> 11) +
							((tbi2 & 0b1111100000000000) >> 11) +
							(((tbi2 >> 16) & 0b1111100000000000) >> 11)) / 4;

						int offset = y / 2 * m_frameBufferPaddedWidth + x;
						buffer_16p[offset] = (r1 << 10 | r2 << 5 | r3);
					}

					m_imageMap.imageMap[((y / m_imageMap.pixelsPerRegion) * ((m_modeInfo.width + (m_imageMap.pixelsPerRegion - 1)) / m_imageMap.pixelsPerRegion)) +
						((x * 2) / m_imageMap.pixelsPerRegion)] = true;
				}
			}
		}

		if (m_frameRect.top == m_modeInfo.height){
			m_frameRect.left = m_frameRect.right = m_frameRect.top = m_frameRect.bottom = 0;
		}
		else{
			m_frameRect.right = (m_frameRect.right + 1) * 2;
			m_frameRect.bottom++;
		}
	}
	else if( m_bandwidthMode && !m_bandwidthController.largeFrameMode) {
		m_frameRect.top = m_modeInfo.height;
		m_frameRect.bottom = -1;
		m_frameRect.left = m_modeInfo.width / 2;
		m_frameRect.right = -1;

		for (int y = 0; y < m_modeInfo.height; y++) {
			for (int x = 0; x < (m_modeInfo.width / 2); x++) {
				if (back_image_lp[y * lp_padding + x] != image_lp[y * lp_padding + x]) {

					if (m_frameRect.top == m_modeInfo.height) {
						m_frameRect.top = m_frameRect.bottom = y;
						m_frameRect.left = m_frameRect.right = x;
					}
					else {
						if (y > m_frameRect.bottom) m_frameRect.bottom = y;
						if (x < m_frameRect.left) m_frameRect.left = x;
						if (x > m_frameRect.right) m_frameRect.right = x;
					}

					uint32_t tbi = image_lp[y * lp_padding + x];
					buffer_lp[y * lp_padding + x] =
						((tbi & 0b11111) << 10) |
						((tbi & 0b11111000000) >> 1) |
						((tbi & 0x0000ffff) >> 11) |
						((tbi & 0b111110000000000000000) << 10) |
						((tbi & 0b111110000000000000000000000) >> 1) |
						((tbi & 0b11111000000000000000000000000000) >> 11);

					m_imageMap.imageMap[((y / m_imageMap.pixelsPerRegion) * ((m_modeInfo.width + (m_imageMap.pixelsPerRegion - 1)) / m_imageMap.pixelsPerRegion)) +
						((x * 2) / m_imageMap.pixelsPerRegion)] = true;
				}
			}
		}

		if (m_frameRect.top == m_modeInfo.height){
			m_frameRect.left = m_frameRect.right = m_frameRect.top = m_frameRect.bottom = 0;
		}
		else{
			m_frameRect.right = (m_frameRect.right + 1) * 2;
			m_frameRect.bottom++;
		}
	}
	else {

		m_frameRect.top = m_modeInfo.height - 1;
		m_frameRect.bottom = 0;
		m_frameRect.left = m_modeInfo.width - 1;
		m_frameRect.right = 0;

		for (int i = 0; i < m_modeInfo.height - 1; i++) {
			if (!std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				m_frameRect.top = i;
				break;
			}
		}

		for (int i = m_modeInfo.height - 1; i >= m_frameRect.top; i--) {
			if (!std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				m_frameRect.bottom = i + 1;
				break;
			}
		}

		m_frameRect.left = 0;
		m_frameRect.right = m_modeInfo.width - 1;

		if (m_frameRect.top < m_frameRect.bottom) {
			if (m_bandwidthMode)
			{
				// Mark all lines in range as changed in imageMap
				for (int i = ((m_frameRect.top / m_imageMap.pixelsPerRegion) * ((m_modeInfo.width + (m_imageMap.pixelsPerRegion - 1)) / m_imageMap.pixelsPerRegion));
						i < ((m_frameRect.bottom / m_imageMap.pixelsPerRegion) * ((m_modeInfo.width + (m_imageMap.pixelsPerRegion - 1)) / m_imageMap.pixelsPerRegion)) - 1;
						i++){
					m_imageMap.imageMap[i] = true;
				}
			}

			std::transform(image_lp + (m_frameRect.top * lp_padding), image_lp + (m_frameRect.bottom * lp_padding - 1), buffer_lp + (m_frameRect.top * lp_padding),
				[](uint32_t tbi){
				return
					((tbi & 0b11111) << 10) |
					((tbi & 0b11111000000) >> 1) |
					((tbi & 0x0000ffff) >> 11) |
					((tbi & 0b111110000000000000000) << 10) |
					((tbi & 0b111110000000000000000000000) >> 1) |
					((tbi & 0b11111000000000000000000000000000) >> 11);
			});
		}
		else {
			m_frameRect.left = m_frameRect.right = m_frameRect.top = m_frameRect.bottom = 0;
		}
	}

	std::swap(m_back_image, m_image);

	m_fpsCounter.Frame();

	char printbuffer[80];
	sprintf(printbuffer, "Picture (%03d fps) left=%d, top=%d, right=%d, bottom=%d              \r",
		m_fpsCounter.GetFPS(), m_frameRect.left, m_frameRect.top, m_frameRect.right, m_frameRect.bottom);

	if (m_lastPrintedMessage != printbuffer) {
		std::cout << printbuffer;
		m_lastPrintedMessage = printbuffer;
	}

	/* success!   We have a new picture! */
	return (m_frameRect.top != m_frameRect.bottom);
}

void DMXVNCServer::DoPtr(int buttonMask, int x, int y, rfbClientPtr cl)
{
	auto server = reinterpret_cast<DMXVNCServer *>(cl->screen->screenData);

	if (server->m_downscale) {
		x *= 2;
		y *= 2;
	}

	server->m_mouse.DoPtr(buttonMask, x, y);
}

void DMXVNCServer::DoKey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	auto server = reinterpret_cast<DMXVNCServer *>(cl->screen->screenData);
	server->m_keyboard.DoKey(down, key);
}

enum rfbNewClientAction DMXVNCServer::newclient(rfbClientPtr cl)
{
	auto server = reinterpret_cast<DMXVNCServer *>(cl->screen->screenData);
	return server->NewClient(cl);
}

enum rfbNewClientAction DMXVNCServer::NewClient(rfbClientPtr cl)
{
	m_clientCount++;
	cl->clientGoneHook = clientgone;
	return RFB_CLIENT_ACCEPT;
}

void DMXVNCServer::clientgone(rfbClientPtr cl)
{
	auto server = reinterpret_cast<DMXVNCServer *>(cl->screen->screenData);
	server->ClientGone(cl);
}

void DMXVNCServer::ClientGone(rfbClientPtr cl)
{
	m_clientCount--;
}

void ImageMap::Resize(int height, int width)
{
	this->height = height;
	this->width = width;
	mapWidth = ((width + (pixelsPerRegion - 1)) / pixelsPerRegion);
	mapHeight = ((height + (pixelsPerRegion - 1)) / pixelsPerRegion);
	imageMap.resize(mapHeight * mapWidth);
}

void ImageMap::Clear()
{
	std::fill(imageMap.begin(), imageMap.end(), false);
	changedRegionRatio = -1;
}

int ImageMap::GetChangedRegionRatio()
{
	if (changedRegionRatio == -1)
		changedRegionRatio = std::count(imageMap.begin(), imageMap.end(), true) * 100 / imageMap.size();
	return changedRegionRatio;
}

void BandwidthController::ControlMode(int changedRegionRatio)
{
	if (largeFrameMode) {
		if (changedRegionRatio < smallUpdateFramesSize) {
			smallUpdateFrames++;
			largeUpdateFrames = 0;
			if (smallUpdateFrames > smallUpdateFramesSwitchCount) {
				largeFrameMode = false;
			}
		}
	}
	else {
		if (changedRegionRatio > largeUpdateFramesSize) {
			smallUpdateFrames = 0;
			largeUpdateFrames++;
			if (largeUpdateFrames > largeUpdateFramesSwitchCount) {
				largeFrameMode = true;
			}
		}
	}
}

