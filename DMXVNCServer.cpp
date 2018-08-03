#include "DMXVNCServer.hpp"

#include "Exception.hpp"

#undef max

char localhost_address[] = "localhost";

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

bool terminate = false;

double getTime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec + (now.tv_usec / 1000000.0);
};


DMXVNCServer::DMXVNCServer(int BPP, int frameRate) {
	this->BPP = BPP;
	this->targetPictureTimeout = 1.0 / (float)frameRate;
}

DMXVNCServer::~DMXVNCServer()
{
	Close();
	if (server) {
		rfbShutdownServer(server, TRUE);
		rfbScreenCleanup(server);
		server = nullptr;
	};

	if(we.we_offs) {
		wordfree(&we);
	}
}

void DMXVNCServer::Open()
{
	m_display.Open(screen);
	m_display.GetInfo(info);

	printf("info: %d, %d, %d, %d\n", info.width, info.height, info.transform, info.input_format);

	/* DispmanX expects buffer rows to be aligned to a 32 bit boundarys */
	pitch = ALIGN_UP(BPP * info.width, 32);
	padded_width = pitch / BPP;

	if (m_downscale) {
		frameBufferPitch = ALIGN_UP(BPP * info.width / 2, 32);
		frameBufferPaddedWidth = frameBufferPitch / BPP;
	}
	else {
		frameBufferPitch = pitch;
		frameBufferPaddedWidth = padded_width;
	}

	printf("Display is %d x %d\n", info.width, info.height);

	imageBuffer1.resize(pitch * info.height);
	std::fill(imageBuffer1.begin(), imageBuffer1.end(), '\0');
	imageBuffer2.resize(pitch * info.height);
	std::fill(imageBuffer2.begin(), imageBuffer2.end(), '\0');
	image = &imageBuffer1[0];
	back_image = &imageBuffer2[0];

	r_x0 = r_y0 = 0;
	r_x1 = info.width;
	r_y1 = info.height;

	m_resource.Create(type, info.width, info.height, &vc_image_ptr);

	m_mouse.SetLastX(padded_width / 2);
	m_mouse.SetLastY(info.height / 2);
}

void DMXVNCServer::Close()
{
	m_resource.Close();
	m_display.Close();
	imageBuffer1.resize(0);
	imageBuffer2.resize(0);
	image = nullptr;
	back_image = nullptr;
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
	long usec;

	this->safeMode = safeMode;
	this->bandwidthMode = bandwidthMode;
	this->screen = screen;
	this->multiThreaded = multiThreaded;
	m_downscale = downscale;

	Open();

	we.we_offs = 1;
	wordexp(vncParams.c_str(), &we, WRDE_DOOFFS);
	int argc = we.we_offs + we.we_wordc;
	std::vector<char *> argv(argc);

	for(int i = 0; i < argc; i++) {
		argv[i] = we.we_wordv[i];
	}	

	if (m_downscale) {
		server = rfbGetScreen(&argc, &argv[0], frameBufferPaddedWidth, info.height / 2, 5, 3, BPP);
	}
	else {
		server = rfbGetScreen(&argc, &argv[0], frameBufferPaddedWidth, info.height, 5, 3, BPP);
	}
	
	if (!server)
		throw Exception("rfbGetScreen failed");

	if (port){
		server->port = port;
	}
	
	if(localhost) {
		server->listen6Interface = localhost_address;
		rfbStringToAddr(localhost_address, &server->listenInterface);
	}

	if (password.length()) {
		this->password = password;
		passwords[0] = this->password.c_str();
		server->authPasswdData = (void *)passwords;
		server->passwordCheck = rfbCheckPasswordByList;
	}

	char hostname[HOST_NAME_MAX + 1];
	if (0 == gethostname(hostname, sizeof(hostname))){
		desktopName = hostname;
		desktopName += " : ";
		desktopName += std::to_string(screen);
		server->desktopName = desktopName.c_str();
	}
	else
		server->desktopName = "VNC server via dispmanx";

	if (m_downscale)
		frameBuffer.resize(frameBufferPitch*info.height / 2);
	else
		frameBuffer.resize(frameBufferPitch*info.height);
	server->frameBuffer = (char*)&frameBuffer[0];
	server->alwaysShared = (1 == 1);
	server->kbdAddEvent = DoKey;
	server->ptrAddEvent = DoPtr;
	server->newClientHook = newclient;
	server->screenData = this;

	/*
	server->serverFormat.redShift = 11;
	server->serverFormat.blueShift = 0;
	server->serverFormat.greenShift = 6;
	*/
	printf("Server bpp:%d\n", server->serverFormat.bitsPerPixel);
	printf("Server bigEndian:%d\n", server->serverFormat.bigEndian);
	printf("Server redShift:%d\n", server->serverFormat.redShift);
	printf("Server blueShift:%d\n", server->serverFormat.blueShift);
	printf("Server greeShift:%d\n", server->serverFormat.greenShift);

	/* Initialize the server */
	rfbInitServer(server);
	if (multiThreaded)
		rfbRunEventLoop(server,-1,TRUE);

	m_mouse.Open(relativeMode, info.width, info.height);
	m_keyboard.Open();

	if (bandwidthMode)
		imageMap.Resize(info.height, info.width);

	/* Loop, processing clients and taking pictures */
	int errors = 0;
	bool toggledDownscale = false;
	while (!terminate && rfbIsActive(server)) {
		double timeToTakePicture = 0.0;
		if (clients && 0.0 == (timeToTakePicture = TimeToTakePicture())) {
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
					if (toggledDownscale ||	info.width != server->width || info.height != server->height) {
						if (m_downscale) {
							frameBuffer.resize(frameBufferPitch*info.height / 2);
							std::fill(frameBuffer.begin(), frameBuffer.end(), '\0');
							rfbNewFramebuffer(server, &frameBuffer[0], frameBufferPaddedWidth, info.height / 2, 5, 3, BPP);
						}
						else {
							frameBuffer.resize(frameBufferPitch*info.height);
							std::fill(frameBuffer.begin(), frameBuffer.end(), '\0');
							rfbNewFramebuffer(server, &frameBuffer[0], frameBufferPaddedWidth, info.height, 5, 3, BPP);
						}
						imageMap.Resize(info.height, info.width);
						toggledDownscale = false;
					}
				}

				if (TakePicture((unsigned char *)server->frameBuffer)) {
					timeLastFrameChange = getTime();
					pictureTimeout = targetPictureTimeout;
					if (bandwidthMode && !bandwidthController.largeFrameMode && imageMap.GetChangedRegionRatio() < bandwidthController.largeUpdateFramesSize) {
						for (int y = 0; y < imageMap.mapHeight; y++){
							for (int x = 0; x < imageMap.mapWidth; x++){
								if (imageMap.imageMap[(y * imageMap.mapWidth) + x]) {
									if (m_downscale){
										rfbMarkRectAsModified(server,
											std::max(r_x0 / 2, x * imageMap.pixelsPerRegion / 2),
											std::max(r_y0 / 2, y * imageMap.pixelsPerRegion / 2),
											std::min(r_x1 / 2, (x * imageMap.pixelsPerRegion) / 2 + imageMap.pixelsPerRegion / 2),
											std::min(r_y1 / 2, (y * imageMap.pixelsPerRegion) / 2 + imageMap.pixelsPerRegion / 2));
									}
									else{
										rfbMarkRectAsModified(server,
											std::max(r_x0, x * imageMap.pixelsPerRegion),
											std::max(r_y0, y * imageMap.pixelsPerRegion),
											std::min(r_x1, (x * imageMap.pixelsPerRegion) + imageMap.pixelsPerRegion),
											std::min(r_y1, (y * imageMap.pixelsPerRegion) + imageMap.pixelsPerRegion));
									}
								}
							}
						}
					}
					else
					{
						if (m_downscale) {
							rfbMarkRectAsModified(server, r_x0 / 2, r_y0 / 2, r_x1 / 2, r_y1 / 2);
						}
						else {
							rfbMarkRectAsModified(server, r_x0, r_y0, r_x1, r_y1);
						}
					}
				}
				else {
					if (targetPictureTimeout < idlePictureTimeout)
						pictureTimeout = std::min((double)idlePictureTimeout, targetPictureTimeout * (std::max(1.0, getTime() - timeLastFrameChange)));;
				}

				if (bandwidthMode) {
					bandwidthController.ControlMode(imageMap.GetChangedRegionRatio());
				}

				errors = 0;
			}
			catch (Exception& e) {
				std::cerr << "Caught exception: " << e.what() << "\n";
				errors++;
				if (errors > 10)
					throw e;
				Close();
			}
		}

		if (!clients) {
			usec = 100 * 1000;
			if (IsOpen())
				Close();
		}
		else {
			usec = (long)std::max(timeToTakePicture * 1000000.0, server->deferUpdateTime * 1000.0);
		}

		if (multiThreaded)
			usleep(usec);
		else {
			if( rfbProcessEvents(server, usec))
				pictureTimeout = targetPictureTimeout;
		}
	}
}

/*
* throttle camera updates
*/
double DMXVNCServer::TimeToTakePicture() {
	double now = getTime();
	double elapsed = now - timeLastFrameStart;

	if (elapsed > pictureTimeout)
		timeLastFrameStart = now;
	return std::max(0.0, pictureTimeout - elapsed);
};

/*
* simulate grabbing a picture from some device
*/
int DMXVNCServer::TakePicture(unsigned char *buffer)
{
	static int last_line = 0, fps = 0, fcount = 0;
	int line = 0;
	struct timeval now;

	DISPMANX_TRANSFORM_T	transform = (DISPMANX_TRANSFORM_T)0;
	VC_RECT_T			rect;

	if (safeMode)
	{
		DISPMANX_MODEINFO_T info;
		m_display.GetInfo(info);
		if (info.width != this->info.width || info.height != this->info.height)
			throw Exception("New mode detected");
	}

	m_display.Snapshot(m_resource, transform);

	vc_dispmanx_rect_set(&rect, 0, 0, info.width, info.height);
	m_resource.ReadData(rect, image, pitch);

	unsigned long *image_lp = (unsigned long *)image;
	unsigned long *buffer_lp = (unsigned long *)buffer;
	unsigned long *back_image_lp = (unsigned long*)back_image;

	int lp_padding = padded_width >> 1;

	if (bandwidthMode){
		imageMap.Clear();
	}

	if (m_downscale) {
		uint16_t *buffer_16p = (uint16_t *)buffer;

		r_y0 = info.height;
		r_y1 = -1;
		r_x0 = info.width / 2;
		r_x1 = -1;

		for (int y = 0; y < info.height; y += 2) {
			for (int x = 0; x < (info.width / 2); x++) {
				if (back_image_lp[y * lp_padding + x] != image_lp[y * lp_padding + x] ||
					back_image_lp[(y + 1) * lp_padding + x] != image_lp[(y + 1) * lp_padding + x]) {

					if (r_y0 == info.height) {
						r_y0 = r_y1 = y;
						r_x0 = r_x1 = x;
					}
					else {
						if (y > r_y1) r_y1 = y + 1;
						if (x < r_x0) r_x0 = x;
						if (x > r_x1) r_x1 = x;
					}

					{
						unsigned long tbi1 = image_lp[y * lp_padding + x];
						unsigned long tbi2 = image_lp[(y + 1) * lp_padding + x];

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

						int offset = y / 2 * frameBufferPaddedWidth + x;
						buffer_16p[offset] = (r1 << 10 | r2 << 5 | r3);
					}

					imageMap.imageMap[((y / imageMap.pixelsPerRegion) * ((info.width + (imageMap.pixelsPerRegion - 1)) / imageMap.pixelsPerRegion)) +
						((x * 2) / imageMap.pixelsPerRegion)] = true;
				}
			}
		}

		if (r_y0 == info.height){
			r_x0 = r_x1 = r_y0 = r_y1 = 0;
		}
		else{
			r_x1 = (r_x1 + 1) * 2;
			r_y1++;
		}
	}
	else if( bandwidthMode && !bandwidthController.largeFrameMode) {
		r_y0 = info.height;
		r_y1 = -1;
		r_x0 = info.width / 2;
		r_x1 = -1;

		for (int y = 0; y < info.height; y++) {
			for (int x = 0; x < (info.width / 2); x++) {
				if (back_image_lp[y * lp_padding + x] != image_lp[y * lp_padding + x]) {

					if (r_y0 == info.height) {
						r_y0 = r_y1 = y;
						r_x0 = r_x1 = x;
					}
					else {
						if (y > r_y1) r_y1 = y;
						if (x < r_x0) r_x0 = x;
						if (x > r_x1) r_x1 = x;
					}

					unsigned long tbi = image_lp[y * lp_padding + x];
					buffer_lp[y * lp_padding + x] =
						((tbi & 0b11111) << 10) |
						((tbi & 0b11111000000) >> 1) |
						((tbi & 0x0000ffff) >> 11) |
						((tbi & 0b111110000000000000000) << 10) |
						((tbi & 0b111110000000000000000000000) >> 1) |
						((tbi & 0b11111000000000000000000000000000) >> 11);

					imageMap.imageMap[((y / imageMap.pixelsPerRegion) * ((info.width + (imageMap.pixelsPerRegion - 1)) / imageMap.pixelsPerRegion)) +
						((x * 2) / imageMap.pixelsPerRegion)] = true;
				}
			}
		}

		if (r_y0 == info.height){
			r_x0 = r_x1 = r_y0 = r_y1 = 0;
		}
		else{
			r_x1 = (r_x1 + 1) * 2;
			r_y1++;
		}
	}
	else {

		r_y0 = info.height - 1;
		r_y1 = 0;
		r_x0 = info.width - 1;
		r_x1 = 0;

		for (int i = 0; i < info.height - 1; i++) {
			if (!std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				r_y0 = i;
				break;
			}
		}

		for (int i = info.height - 1; i >= r_y0; i--) {
			if (!std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				r_y1 = i + 1;
				break;
			}
		}

		r_x0 = 0;
		r_x1 = info.width - 1;

		if (r_y0 < r_y1) {
			if (bandwidthMode)
			{
				// Mark all lines in range as changed in imageMap
				for (int i = ((r_y0 / imageMap.pixelsPerRegion) * ((info.width + (imageMap.pixelsPerRegion - 1)) / imageMap.pixelsPerRegion));
						i < ((r_y1 / imageMap.pixelsPerRegion) * ((info.width + (imageMap.pixelsPerRegion - 1)) / imageMap.pixelsPerRegion)) - 1;
						i++){
					imageMap.imageMap[i] = true;
				}
			}

			std::transform(image_lp + (r_y0 * lp_padding), image_lp + (r_y1 * lp_padding - 1), buffer_lp + (r_y0 * lp_padding),
				[](unsigned long tbi){
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
			r_x0 = r_x1 = r_y0 = r_y1 = 0;
		}
	}

	std::swap(back_image, image);

	gettimeofday(&now, NULL);
	line = now.tv_usec / (1000000 / info.height);
	if (line>info.height) line = info.height - 1;
	//memset(&buffer[(info.width * BPP) * line], 0, (info.width * BPP));
	/* frames per second (informational only) */
	fcount++;
	if (last_line > line) {
		fps = fcount;
		fcount = 0;
	}
	last_line = line;

	char printbuffer[80];
	sprintf(printbuffer, "Picture (%03d fps) x0=%d, y0=%d, x1=%d, y1=%d              \r",
		fps, r_x0, r_y0, r_x1, r_y1);

	if (lastPrintedMessage != printbuffer) {
		fprintf(stderr, printbuffer);
		lastPrintedMessage = printbuffer;
	}

	/* success!   We have a new picture! */
	return (r_y0 != r_y1);
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
	((DMXVNCServer *)(cl->screen->screenData))->m_keyboard.DoKey(down, key);
}

enum rfbNewClientAction DMXVNCServer::newclient(rfbClientPtr cl)
{
	return ((DMXVNCServer *)(cl->screen->screenData))->NewClient(cl);
}

enum rfbNewClientAction DMXVNCServer::NewClient(rfbClientPtr cl)
{
	clients++;
	cl->clientGoneHook = clientgone;
	return RFB_CLIENT_ACCEPT;
}

void DMXVNCServer::clientgone(rfbClientPtr cl)
{
	((DMXVNCServer *)(cl->screen->screenData))->ClientGone(cl);
}

void DMXVNCServer::ClientGone(rfbClientPtr cl)
{
	clients--;
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

