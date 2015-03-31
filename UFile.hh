#ifndef UFILE_HH
#define UFILE_HH

#include <linux/input.h>
#include <linux/uinput.h>

class UFile
{
public:
	~UFile();
	void Open(int relativeMode, int width, int height);
	void Close();
	void WriteEvent(__u16 type, __u16 code, __s32 value);

private:
	int ufile = -1;
};

#endif //UFILE_HH
