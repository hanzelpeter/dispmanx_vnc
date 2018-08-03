#include "DMXMouse.hpp"

void DMXMouse::DoPtr(int buttonMask, int x, int y)
{
	//printf("mouse: 0x%x at %d,%d\n", buttonMask, x,y);

	if (m_relativeMode) {
		m_mouse.WriteEvent(EV_REL, REL_X, x - m_lastX);
		m_mouse.WriteEvent(EV_REL, REL_Y, y - m_lastY);
	}
	else {
		m_mouse.WriteEvent(EV_ABS, ABS_X, x);
		m_mouse.WriteEvent(EV_ABS, ABS_Y, y);
	}

	m_lastX = x;
	m_lastY = y;

	m_mouse.WriteEvent(EV_SYN, SYN_REPORT, 0);
	if (m_lastMask != buttonMask) {
		int left_l = m_lastMask & 0x1;
		int left_w = buttonMask & 0x1;

		if (left_l != left_w) {
			m_mouse.WriteEvent(EV_KEY, BTN_LEFT, left_w);
			m_mouse.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}

		int middle_l = m_lastMask & 0x2;
		int middle_w = buttonMask & 0x2;

		if (middle_l != middle_w) {
			m_mouse.WriteEvent(EV_KEY, BTN_MIDDLE, middle_w >> 1);
			m_mouse.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}
		int right_l = m_lastMask & 0x4;
		int right_w = buttonMask & 0x4;

		if (right_l != right_w) {
			m_mouse.WriteEvent(EV_KEY, BTN_RIGHT, right_w >> 2);
			m_mouse.WriteEvent(EV_SYN, SYN_REPORT, 0);
		}

		m_lastMask = buttonMask;
	}
}

