#pragma once

#include "UFile.hh"

class DMXMouse
{
public:
	void Open(bool relativeMode, int width, int height)
	{
		m_relativeMode = relativeMode;
		m_mouse.Open(relativeMode, width, height);
	}

	void DoPtr(int buttonMask, int x, int y);
	void SetLastX(int x) { m_lastX = x; };
	void SetLastY(int y) { m_lastY = y; };

private:
	UFile m_mouse;

	bool m_relativeMode = false;
	int m_lastMask = 0;
	int m_lastX = 0;
	int m_lastY = 0;
};
