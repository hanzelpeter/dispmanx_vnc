CC = g++
CFLAGS = -g -Wall -std=c++11 -O3 -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM

INCLUDES = -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
LIB_PATHS = -L/opt/vc/lib/
LIBS = -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lvncserver

SOURCES = main.cpp \
		UFile.cpp \
		DMXResource.cpp \
		DMXDisplay.cpp \
		DMXVNCServer.cpp

OBJS = $(SOURCES:.cpp=.o)

MAIN = dispmanx_vncserver

.PHONY: depend clean

all:	$(MAIN)

$(MAIN): $(OBJS)
		 $(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) $(LIB_PATHS) $(LIBS)

.cpp.o:
		$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean: 
		$(RM) *.o *~ $(MAIN)

depend: $(SOURCES)
		makedepend $(INCLUDES) $^

# DO NOT DELETE 
