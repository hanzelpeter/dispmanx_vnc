// VNC server using dispmanx
// TODO: mouse support with inconsistency between fbdev and dispmanx

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <exception>

#include <rfb/rfb.h>
#include <rfb/keysym.h>

#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <unistd.h>
#include "bcm_host.h"
#include <assert.h>

#define BPP      2

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

/* 15 frames per second (if we can) */
#define PICTURE_TIMEOUT (1.0/15.0)

int terminate = 0;

/* for compatibility of non-android systems */
#ifndef ANDROID
# define KEY_SOFT1 KEY_UNKNOWN
# define KEY_SOFT2 KEY_UNKNOWN
# define KEY_CENTER	KEY_UNKNOWN
# define KEY_SHARP KEY_UNKNOWN
# define KEY_STAR KEY_UNKNOWN
#endif


int mouse_last = 0;

int last_x;
int last_y;


static int clients = 0;

static void clientgone(rfbClientPtr cl)
{
	clients--;
}

static enum rfbNewClientAction newclient(rfbClientPtr cl)
{
	clients++;
	cl->clientGoneHook = clientgone;
	return RFB_CLIENT_ACCEPT;
}

void sig_handler(int signo)
{
	terminate = 1;
}

class Exception : public std::exception
{
public:
	Exception(const char *whatString)
		: m_whatString(whatString)
	{

	}

	const char *what() const noexcept override
	{
		return m_whatString;
	}

private:
	const char *m_whatString = "";
};

class UFile
{
public:
	~UFile()
	{
		Close();
	}

	void Open(int relativeMode, int width, int height)
	{
		struct uinput_user_dev uinp;
		int retcode, i;

		ufile = open("/dev/uinput", O_WRONLY | O_NDELAY);
		printf("open /dev/uinput returned %d.\n", ufile);
		if (ufile == 0) {
			throw Exception("Could not open uinput.\n");
		}

		memset(&uinp, 0, sizeof(uinp));
		strncpy(uinp.name, "VNCServer SimKey", 20);
		uinp.id.version = 4;
		uinp.id.bustype = BUS_USB;

		if (!relativeMode) {
			uinp.absmin[ABS_X] = 0;
			uinp.absmax[ABS_X] = width;
			uinp.absmin[ABS_Y] = 0;
			uinp.absmax[ABS_Y] = height;
		}

		ioctl(ufile, UI_SET_EVBIT, EV_KEY);

		for (i = 0; i<KEY_MAX; i++) { //I believe this is to tell UINPUT what keys we can make?
			ioctl(ufile, UI_SET_KEYBIT, i);
		}

		ioctl(ufile, UI_SET_KEYBIT, BTN_LEFT);
		ioctl(ufile, UI_SET_KEYBIT, BTN_RIGHT);
		ioctl(ufile, UI_SET_KEYBIT, BTN_MIDDLE);

		if (relativeMode) {
			ioctl(ufile, UI_SET_EVBIT, EV_REL);
			ioctl(ufile, UI_SET_RELBIT, REL_X);
			ioctl(ufile, UI_SET_RELBIT, REL_Y);
		}
		else {
			ioctl(ufile, UI_SET_EVBIT, EV_ABS);
			ioctl(ufile, UI_SET_ABSBIT, ABS_X);
			ioctl(ufile, UI_SET_ABSBIT, ABS_Y);
		}

		retcode = write(ufile, &uinp, sizeof(uinp));
		printf("First write returned %d.\n", retcode);

		retcode = (ioctl(ufile, UI_DEV_CREATE));
		printf("ioctl UI_DEV_CREATE returned %d.\n", retcode);
		if (retcode) {
			printf("Error create uinput device %d.\n", retcode);
			exit(-1);
		}
	}

	void Close()
	{
		if (ufile != -1)
		{
			// destroy the device
			ioctl(ufile, UI_DEV_DESTROY);
			close(ufile);
			ufile = -1;
		}
	}

	void WriteEvent(__u16 type, __u16 code, __s32 value)
	{
		struct input_event inputEvent = { 0 };
		gettimeofday(&inputEvent.time, NULL);
		inputEvent.type = type;
		inputEvent.code = code;
		inputEvent.value = value;
		write(ufile, &inputEvent, sizeof(inputEvent));
	}

private:
	int ufile = -1;
};

class DMXResource
{
public:
	~DMXResource()
	{
		Close();
	}

	void Create(VC_IMAGE_TYPE_T type, int width, int height, uint32_t *vc_image_ptr)
	{
		m_resource = vc_dispmanx_resource_create(type, width, height, vc_image_ptr);
		if (!m_resource)
			throw Exception("vc_dispmanx_resource_create failed");
	}

	void Close()
	{
		if (m_resource)
		{
			int ret = vc_dispmanx_resource_delete(m_resource);
			m_resource = DISPMANX_NO_HANDLE;
		}
	}

	void ReadData(VC_RECT_T& rect, void *image, int pitch)
	{
		vc_dispmanx_resource_read_data(m_resource, &rect, image, pitch);
	}

	DISPMANX_RESOURCE_HANDLE_T GetResourceHandle()
	{
		return m_resource;
	}

private:
	DISPMANX_RESOURCE_HANDLE_T  m_resource = DISPMANX_NO_HANDLE;
};

class DMXDisplay
{
public:
	~DMXDisplay()
	{
		Close();
	}

	void Open( int screen)
	{
		printf("Open display[%i]...\n", screen);
		m_display = vc_dispmanx_display_open(screen);
		if (m_display == DISPMANX_NO_HANDLE || m_display == DISPMANX_INVALID)
		{
			throw Exception("vc_dispmanx_display_open failed");
		}
	}

	void Close()
	{
		if (m_display != DISPMANX_NO_HANDLE)
		{
			int ret = vc_dispmanx_display_close(m_display);
			m_display = DISPMANX_NO_HANDLE;
		}
	}

	void GetInfo(DISPMANX_MODEINFO_T& info)
	{
		int ret = vc_dispmanx_display_get_info(m_display, &info);
		if (ret != 0)
			throw Exception("vc_dispmanx_display_get_info failed");
	}

	void Snapshot(DMXResource& resource, DISPMANX_TRANSFORM_T transform)
	{
		int ret = vc_dispmanx_snapshot( m_display, resource.GetResourceHandle(), transform);
		if (ret != 0)
			throw Exception("vc_dispmanx_snapshot failed");
	}

private:
	DISPMANX_DISPLAY_HANDLE_T m_display = DISPMANX_NO_HANDLE;
};


class DMXVNCServer
{
public:
	~DMXVNCServer()
	{
		Close();
		if (image) {
			free(image);
			image = nullptr;
		}

		if (back_image){
			free(back_image);
			back_image = nullptr;
		}
	}

	void Init()
	{
		DISPMANX_MODEINFO_T info = { 0 };

		m_display.Open(screen);
		m_display.GetInfo(info);

		printf("info: %d, %d, %d, %d\n", info.width, info.height, info.transform, info.input_format);

		if (info.width != this->info.width || info.height != this->info.height)
		{
			if (image) {
				free(image);
				image = nullptr;
			}
			if (back_image) {
				free(back_image);
				back_image = nullptr;
			}
		}

		this->info = info;

		/* DispmanX expects buffer rows to be aligned to a 32 bit boundarys */
		pitch = ALIGN_UP(2 * info.width, 32);
		padded_width = pitch / BPP;

		printf("Display is %d x %d\n", info.width, info.height);

		if (!image) {
			image = calloc(1, pitch * info.height);
			assert(image);
		}

		if (!back_image) {
			back_image = calloc(1, pitch * info.height);
			assert(back_image);
		}

		r_x0 = r_y0 = 0;
		r_x1 = info.width;
		r_y1 = info.height;

		m_resource.Create(type, info.width, info.height, &vc_image_ptr);

		last_x = padded_width / 2;
		last_y = info.height / 2;
	}

	void Close()
	{
		m_resource.Close();
		m_display.Close();
	}

	void Run(int argc, char *argv[], int port, const char *password, int screen, int relativeMode)
	{
		int             		ret, end, x;
		long usec;

		this->relativeMode = relativeMode;
		this->screen = screen;

		bcm_host_init();

		Init();

		rfbScreenInfoPtr server = rfbGetScreen(&argc, argv, padded_width, info.height, 5, 3, BPP);
		if (!server)
			throw Exception( "rfbGetScreen failed");

		if (port){
			server->port = port;
		}

		if (*password) {
			static const char *passwords[] = { nullptr, nullptr };
			passwords[0] = password;
			server->authPasswdData = (void *)passwords;
			server->passwordCheck = rfbCheckPasswordByList;
		}

		server->desktopName = "VNC server via dispmanx";
		server->frameBuffer = (char*)malloc(pitch*info.height);
		server->alwaysShared = (1 == 1);
		server->kbdAddEvent = dokey;
		server->ptrAddEvent = doptr;
		server->newClientHook = newclient;
		server->screenData = this;

		printf("Server bpp:%d\n", server->serverFormat.bitsPerPixel);
		printf("Server bigEndian:%d\n", server->serverFormat.bigEndian);
		printf("Server redShift:%d\n", server->serverFormat.redShift);
		printf("Server blueShift:%d\n", server->serverFormat.blueShift);
		printf("Server greeShift:%d\n", server->serverFormat.greenShift);

		/* Initialize the server */
		rfbInitServer(server);

		m_ufile.Open(this->relativeMode, info.width, info.height);

		/* Loop, processing clients and taking pictures */
		while (!terminate && rfbIsActive(server)) {
			if (clients && TimeToTakePicture()) {
				try {
					if (TakePicture((unsigned char *)server->frameBuffer)) {
						rfbMarkRectAsModified(server, r_x0, r_y0, r_x1, r_y1);
					}
				}
				catch (Exception& e) {
					std::cerr << "Caught exception: " << e.what() << "\n";
					Close();
					Init();

					if (info.width != server->width || info.height != server->height) {
						void *oldFrameBuffer = server->frameBuffer;
						char *frameBuffer = (char*)malloc(pitch*info.height);
						rfbNewFramebuffer(server, frameBuffer, padded_width, info.height, 5, 3, BPP);
						free(oldFrameBuffer);
					}
				}
			}

			usec = server->deferUpdateTime * 1000;
			rfbProcessEvents(server, usec);
		}

		rfbShutdownServer(server, TRUE);
		void *oldFrameBuffer = server->frameBuffer;
		rfbScreenCleanup(server);
		free(oldFrameBuffer);

		bcm_host_deinit();
	}

	/*
	* throttle camera updates
	*/
	int TimeToTakePicture() {
		static struct timeval now = { 0, 0 }, then = { 0, 0 };
		double elapsed, dnow, dthen;

		gettimeofday(&now, NULL);

		dnow = now.tv_sec + (now.tv_usec / 1000000.0);
		dthen = then.tv_sec + (then.tv_usec / 1000000.0);
		elapsed = dnow - dthen;

		if (elapsed > PICTURE_TIMEOUT)
			memcpy((char *)&then, (char *)&now, sizeof(struct timeval));
		return elapsed > PICTURE_TIMEOUT;
	}

	/*
	* simulate grabbing a picture from some device
	*/
	int TakePicture(unsigned char *buffer)
	{
		static int last_line = 0, fps = 0, fcount = 0;
		int line = 0;
		int i, j;
		struct timeval now;

		DISPMANX_TRANSFORM_T	transform = (DISPMANX_TRANSFORM_T)0;
		VC_RECT_T			rect;

		m_display.Snapshot(m_resource, transform);

		vc_dispmanx_rect_set(&rect, 0, 0, info.width, info.height);
		m_resource.ReadData(rect, image, pitch);

		unsigned short *image_p = (unsigned short *)image;
		unsigned short *buffer_p = (unsigned short *)buffer;
		unsigned short *back_image_p = (unsigned short *)back_image;

		unsigned long *image_lp = (unsigned long *)image_p;
		unsigned long *buffer_lp = (unsigned long *)buffer_p;
		unsigned long *back_image_lp = (unsigned long*)back_image_p;

		int lp_padding = padded_width >> 1;

		r_y0 = info.height - 1;
		r_y1 = 0;
		r_x0 = info.width - 1;
		r_x1 = 0;

		for (i = 0; i < info.height - 1; i++) {
			if (!std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				r_y0 = i;
				break;
			}
		}

		for (i = info.height - 1; i >= r_y0; i--) {
			if( !std::equal(back_image_lp + i * lp_padding, back_image_lp + i * lp_padding + lp_padding - 1, image_lp + i * lp_padding)) {
				r_y1 = i + 1;
				break;
			}

		}

		r_x0 = 0;
		r_x1 = info.width - 1;

		if (r_y0 <= r_y1) {
			for (j = r_y0; j<r_y1; ++j) {
				for (i = r_x0; i<r_x1; ++i) {
					int pos = j * padded_width + i;
					unsigned short	tbi = image_p[pos];
					unsigned short val;
					val = ((tbi & 0b11111) << 10);
					val |= ((tbi & 0b11111000000) >> 1);
					val |= (tbi >> 11);
					buffer_p[pos] = val;
				}
				/*
				for (i = 0; i<lp_padding; i++) {
				int pos = j * lp_padding + i;
				unsigned long tbi = image_lp[pos];
				unsigned long val;

				val = ((tbi & 0b11111) << 10);
				val |= ((tbi & 0b11111000000) >> 1);
				val |= ((tbi & 0x0000ffff)>> 11);

				val |= ((tbi & 0b111110000000000000000) << 10);
				val |= ((tbi & 0b111110000000000000000000000) >> 1);
				val |= ((tbi & 0b11111000000000000000000000000000) >> 11);

				buffer_lp[pos] = val;
				}*/
			}

			// This didn't work, colors were not correct
			/*for (j = r_y0; j < r_y1; j++) {
				for (i = r_x0 >> 1; i<r_x1 >> 1; i++) {
					register unsigned long tbi = image_lp[i + (j*lp_padding)];
					buffer_lp[i + (j*lp_padding)] = tbi; // | mask;
				}
			}*/
		}
		else {
			r_y1 = r_y0;
		}

		void *tmp_image = back_image;
		back_image = image;
		image = tmp_image;

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

	int keysym2scancode(rfbKeySym key)
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

	static void doptr(int buttonMask, int x, int y, rfbClientPtr cl)
	{
		((DMXVNCServer *)(cl->screen->screenData))->DoPtr(buttonMask, x, y, cl);
	}

	void DoPtr(int buttonMask, int x, int y, rfbClientPtr cl)
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

	static void dokey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
	{
		((DMXVNCServer *)(cl->screen->screenData))->DoKey(down, key, cl);
	}

	void DoKey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
	{
		struct input_event       event;

		if (down) {
			m_ufile.WriteEvent(EV_KEY, keysym2scancode(key), 1);
			m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}
		else {
			m_ufile.WriteEvent(EV_KEY, keysym2scancode(key), 0);
			m_ufile.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}
	}

private:
	DMXDisplay m_display;
	DMXResource m_resource;
	UFile m_ufile;

	DISPMANX_MODEINFO_T info = { 0 };
	int relativeMode = 0;
	int screen = 0;

	int padded_width = 0;
	int pitch = 0;
	int r_x0 = 0;
	int r_y0 = 0;
	int r_x1 = 0;
	int r_y1 = 0;

	VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;
	uint32_t  vc_image_ptr = 0;

	void *image = nullptr;
	void *back_image = nullptr;
};



int main(int argc, char *argv[])
{
	try
	{
		uint32_t screen = 0;
		int relativeMode = 0;
		const char *password = "";
		int port = 0;

		for (int x = 1; x < argc; x++) {
			if (strcmp(argv[x], "-r") == 0)
				relativeMode = 1;
			if (strcmp(argv[x], "-a") == 0)
				relativeMode = 0;
			if (strcmp(argv[x], "-P") == 0) {
				password = argv[x + 1];
				x++;
			}
			if (strcmp(argv[x], "-p") == 0) {
				port = atoi(argv[x + 1]);
				x++;
			}
		}

		if (signal(SIGINT, sig_handler) == SIG_ERR) {
			fprintf(stderr, "error setting sighandler\n");
			exit(-1);
		}

		DMXVNCServer vncServer;
		vncServer.Run( argc, argv, port, password, screen, relativeMode);
	}
	catch (Exception& e)
	{
		std::cerr << "Exception caught: " << e.what() << "\n";
	}

	printf("\nDone\n");

	return 0;

}

