#pragma once

#include <linux/input.h>
#include <linux/uinput.h>
#include <string>

class UFile
{
public:
	~UFile();
	void Open(bool relativeMode = false, int width = 0, int height = 0);
	void Close();
	void WriteEvent(__u16 type, __u16 code, __s32 value);

private:
	std::string m_name;
	int m_ufile = -1;
};
