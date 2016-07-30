#
#  Makefile
#
#  Build dependencies: libvncserver-dev and libraspberrypi-dev
#

APP:=dispmanx_vncserver

all: $(APP)

$(APP): main.c
	gcc -O3 main.c -o $(APP) -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -L/opt/vc/lib/ -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lvncserver
