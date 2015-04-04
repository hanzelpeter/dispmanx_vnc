#include "DMXVNCServer.hh"

#include "Exception.hh"

#undef max

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

/* for compatibility of non-android systems */
#ifndef ANDROID
# define KEY_SOFT1 KEY_UNKNOWN
# define KEY_SOFT2 KEY_UNKNOWN
# define KEY_CENTER	KEY_UNKNOWN
# define KEY_SHARP KEY_UNKNOWN
# define KEY_STAR KEY_UNKNOWN
#endif

bool terminate = false;

double getTime() {
	static struct timeval now = { 0, 0 };
	gettimeofday(&now, NULL);
	return now.tv_sec + (now.tv_usec / 1000000.0);
};


DMXVNCServer::DMXVNCServer(int BPP, float PICTURE_TIMEOUT) {
	this->BPP = BPP;
	this->PICTURE_TIMEOUT = PICTURE_TIMEOUT;
}

DMXVNCServer::~DMXVNCServer()
{
	Close();
	if (server) {
		rfbShutdownServer(server, TRUE);
		rfbScreenCleanup(server);
		server = nullptr;
	};
}

void DMXVNCServer::Open()
{
	m_display.Open(screen);
	m_display.GetInfo(info);

	printf("info: %d, %d, %d, %d\n", info.width, info.height, info.transform, info.input_format);

	/* DispmanX expects buffer rows to be aligned to a 32 bit boundarys */
	pitch = ALIGN_UP(2 * info.width, 32);
	padded_width = pitch / BPP;

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

	last_x = padded_width / 2;
	last_y = info.height / 2;
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

void DMXVNCServer::Run(int argc, char *argv[], int port, const std::string& password, int screen, bool relativeMode, bool safeMode, bool bandwidthMode)
{
	long usec;

	this->relativeMode = relativeMode;
	this->safeMode = safeMode;
	this->bandwidthMode = bandwidthMode;
	this->screen = screen;

	Open();

	server = rfbGetScreen(&argc, argv, padded_width, info.height, 5, 3, BPP);
	if (!server)
		throw Exception("rfbGetScreen failed");

	if (port){
		server->port = port;
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

	frameBuffer.resize(pitch*info.height);
	server->frameBuffer = (char*)&frameBuffer[0];
	server->alwaysShared = (1 == 1);
	server->kbdAddEvent = dokey;
	server->ptrAddEvent = doptr;
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

	m_ufile.Open(this->relativeMode, info.width, info.height);

	if (bandwidthMode)
		imageMap.Resize(info.height, info.width);

	/* Loop, processing clients and taking pictures */
	int errors = 0;
	while (!terminate && rfbIsActive(server)) {
		double timeToTakePicture = 0.0;
		if (clients && 0.0 == (timeToTakePicture = TimeToTakePicture())) {
			try {
				if (!IsOpen())
				{
					Open();
					if (info.width != server->width || info.height != server->height) {
						frameBuffer.resize(pitch*info.height);
						rfbNewFramebuffer(server, &frameBuffer[0], padded_width, info.height, 5, 3, BPP);
						imageMap.Resize(info.height, info.width);
					}
				}

				if (TakePicture((unsigned char *)server->frameBuffer)) {
					if (bandwidthMode && !bandwidthController.largeFrameMode && imageMap.GetChangedRegionRatio() < 30) {
						for (int y = 0; y < imageMap.mapHeight; y++){
							for (int x = 0; x < imageMap.mapWidth; x++){
								if (imageMap.imageMap[(y * imageMap.mapWidth) + x]) {
									rfbMarkRectAsModified(server,
										std::max(r_x0, x * imageMap.pixelsPerRegion),
										std::max(r_y0, y * imageMap.pixelsPerRegion),
										std::min(r_x1, x * imageMap.pixelsPerRegion + imageMap.pixelsPerRegion),
										std::min(r_y1, y * imageMap.pixelsPerRegion + imageMap.pixelsPerRegion));
								}
							}
						}
					}
					else
					{
						rfbMarkRectAsModified(server, r_x0, r_y0, r_x1, r_y1);
					}
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
		rfbProcessEvents(server, usec);
	}
}

/*
* throttle camera updates
*/
double DMXVNCServer::TimeToTakePicture() {
	static struct timeval now = { 0, 0 }, then = { 0, 0 };
	double elapsed, dnow, dthen;

	gettimeofday(&now, NULL);

	dnow = now.tv_sec + (now.tv_usec / 1000000.0);
	dthen = then.tv_sec + (then.tv_usec / 1000000.0);
	elapsed = dnow - dthen;

	if (elapsed > PICTURE_TIMEOUT)
		memcpy((char *)&then, (char *)&now, sizeof(struct timeval));
	return std::max(0.0, PICTURE_TIMEOUT - elapsed);
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

	if (bandwidthMode && !bandwidthController.largeFrameMode){
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

	static char printbufferold[80] = "";
	char printbuffer[80];

	sprintf(printbuffer, "Picture (%03d fps) x0=%d, y0=%d, x1=%d, y1=%d              \r",
		fps, r_x0, r_y0, r_x1, r_y1);

	if (strcmp(printbuffer, printbufferold)) {
		fprintf(stderr, printbuffer);
		strcpy(printbufferold, printbuffer);
	}

	/* success!   We have a new picture! */
	return (r_y0 != r_y1);
}

int DMXVNCServer::keysym2scancode(rfbKeySym key)
{
	//printf("keysym: %04X\n", key);

	int scancode = 0;

	int code = (int)key;
	if (code >= '0' && code <= '9') {
		scancode = (code & 0xF) - 1;
		if (scancode<0) scancode += 10;
		scancode += KEY_1;
	}
	else if (code >= 0xFF50 && code <= 0xFF58) {
		static const uint16_t map[] =
		{ KEY_HOME, KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN,
		KEY_PAGEUP, KEY_PAGEDOWN, KEY_END, 0 };
		scancode = map[code & 0xF];
	}
	else if (code >= 0xFFE1 && code <= 0xFFEE) {
		static const uint16_t map[] =
		{ KEY_LEFTSHIFT, KEY_LEFTSHIFT,
		KEY_LEFTCTRL, KEY_LEFTCTRL,
		KEY_LEFTSHIFT, KEY_LEFTSHIFT,
		0, 0,
		KEY_LEFTALT, KEY_RIGHTALT,
		0, 0, 0, 0 };
		scancode = map[code & 0xF];
	}
	else if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')) {
		static const uint16_t map[] = {
			KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
			KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
			KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
			KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
			KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
		scancode = map[(code & 0x5F) - 'A'];
	}
	else {
		switch (code) {
		case XK_space:    scancode = KEY_SPACE;       break;

		case XK_exclam: scancode = KEY_1; break;
		case XK_at:     scancode = KEY_2; break;
		case XK_numbersign:    scancode = KEY_3; break;
		case XK_dollar:    scancode = KEY_4; break;
		case XK_percent:    scancode = KEY_5; break;
		case XK_asciicircum:    scancode = KEY_6; break;
		case XK_ampersand:    scancode = KEY_7; break;
		case XK_asterisk:    scancode = KEY_8; break;
		case XK_parenleft:    scancode = KEY_9; break;
		case XK_parenright:    scancode = KEY_0; break;
		case XK_minus:    scancode = KEY_MINUS; break;
		case XK_underscore:    scancode = KEY_MINUS; break;
		case XK_equal:    scancode = KEY_EQUAL; break;
		case XK_plus:    scancode = KEY_EQUAL; break;
		case XK_BackSpace:    scancode = KEY_BACKSPACE; break;
		case XK_Tab:    scancode = KEY_TAB; break;

		case XK_braceleft:    scancode = KEY_LEFTBRACE;     break;
		case XK_braceright:    scancode = KEY_RIGHTBRACE;     break;
		case XK_bracketleft:    scancode = KEY_LEFTBRACE;     break;
		case XK_bracketright:    scancode = KEY_RIGHTBRACE;     break;
		case XK_Return:    scancode = KEY_ENTER;     break;

		case XK_semicolon:    scancode = KEY_SEMICOLON;     break;
		case XK_colon:    scancode = KEY_SEMICOLON;     break;
		case XK_apostrophe:    scancode = KEY_APOSTROPHE;     break;
		case XK_quotedbl:    scancode = KEY_APOSTROPHE;     break;
		case XK_grave:    scancode = KEY_GRAVE;     break;
		case XK_asciitilde:    scancode = KEY_GRAVE;     break;
		case XK_backslash:    scancode = KEY_BACKSLASH;     break;
		case XK_bar:    scancode = KEY_BACKSLASH;     break;

		case XK_comma:    scancode = KEY_COMMA;      break;
		case XK_less:    scancode = KEY_COMMA;      break;
		case XK_period:    scancode = KEY_DOT;      break;
		case XK_greater:    scancode = KEY_DOT;      break;
		case XK_slash:    scancode = KEY_SLASH;      break;
		case XK_question:    scancode = KEY_SLASH;      break;
		case XK_Caps_Lock:    scancode = KEY_CAPSLOCK;      break;

		case XK_F1:    scancode = KEY_F1; break;
		case XK_F2:    scancode = KEY_F2; break;
		case XK_F3:    scancode = KEY_F3; break;
		case XK_F4:    scancode = KEY_F4; break;
		case XK_F5:    scancode = KEY_F5; break;
		case XK_F6:    scancode = KEY_F6; break;
		case XK_F7:    scancode = KEY_F7; break;
		case XK_F8:    scancode = KEY_F8; break;
		case XK_F9:    scancode = KEY_F9; break;
		case XK_F10:    scancode = KEY_F10; break;
		case XK_Num_Lock:    scancode = KEY_NUMLOCK; break;
		case XK_Scroll_Lock:    scancode = KEY_SCROLLLOCK; break;

		case XK_Page_Down:    scancode = KEY_PAGEDOWN; break;
		case XK_Insert:    scancode = KEY_INSERT; break;
		case XK_Delete:    scancode = KEY_DELETE; break;
		case XK_Page_Up:    scancode = KEY_PAGEUP; break;
		case XK_Escape:    scancode = KEY_ESC; break;


		case 0x0003:    scancode = KEY_CENTER;      break;
		}
	}

	return scancode;
}

void DMXVNCServer::doptr(int buttonMask, int x, int y, rfbClientPtr cl)
{
	((DMXVNCServer *)(cl->screen->screenData))->DoPtr(buttonMask, x, y, cl);
}

void DMXVNCServer::DoPtr(int buttonMask, int x, int y, rfbClientPtr cl)
{
	//printf("mouse: 0x%x at %d,%d\n", buttonMask, x,y);

	if (relativeMode) {
		m_ufile.WriteEvent(EV_REL, REL_X, x - last_x);
	}
	else {
		m_ufile.WriteEvent(EV_ABS, ABS_X, x);
	}

	if (relativeMode) {
		m_ufile.WriteEvent(EV_REL, REL_Y, y - last_y);
	}
	else {
		m_ufile.WriteEvent(EV_ABS, ABS_Y, y);
	}

	last_x = x;
	last_y = y;

	m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
	if (mouse_last != buttonMask) {
		int left_l = mouse_last & 0x1;
		int left_w = buttonMask & 0x1;

		if (left_l != left_w) {
			m_ufile.WriteEvent(EV_KEY, BTN_LEFT, left_w);
			m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}

		int middle_l = mouse_last & 0x2;
		int middle_w = buttonMask & 0x2;

		if (middle_l != middle_w) {
			m_ufile.WriteEvent(EV_KEY, BTN_MIDDLE, middle_w >> 1);
			m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}
		int right_l = mouse_last & 0x4;
		int right_w = buttonMask & 0x4;

		if (right_l != right_w) {
			m_ufile.WriteEvent(EV_KEY, BTN_RIGHT, right_w >> 2);
			m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}

		mouse_last = buttonMask;
	}
}

void DMXVNCServer::dokey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	((DMXVNCServer *)(cl->screen->screenData))->DoKey(down, key, cl);
}

void DMXVNCServer::DoKey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	if (down) {
		m_ufile.WriteEvent(EV_KEY, keysym2scancode(key), 1);
		m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
	}
	else {
		m_ufile.WriteEvent(EV_KEY, keysym2scancode(key), 0);
		m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
	}
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
}

int ImageMap::GetChangedRegionRatio()
{
	return std::count(imageMap.begin(), imageMap.end(), true) * 100 / imageMap.size();
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

