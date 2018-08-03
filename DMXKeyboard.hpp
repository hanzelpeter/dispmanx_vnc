#pragma once

#include "UFile.hpp"

#include <rfb/rfb.h>

class DMXKeyboard
{
public:
	void Open()
	{
		m_keyboard.Open();
	}

	void DoKey(rfbBool down, rfbKeySym key);

private:
	int KeySymToScanCode(rfbKeySym key);

	bool m_downKeys[KEY_CNT]{};
	UFile m_keyboard;
};
