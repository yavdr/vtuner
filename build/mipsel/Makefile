ARCH=mipsel
-include ../../Make.config

CC=$(CC-$(ARCH))
CXX=$(CXX-$(ARCH))
CFLAGS=-fpic -DHAVE_DVB_API_VERSION=3 -DHAVE_DREAMBOX_HARDWARE $(DBGFLAGS)
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dmm-3

all: ../../dist/$(ARCH)/vtunerd.$(ARCH) ../../dist/$(ARCH)/vtunerc.$(ARCH)

-include ../../Make.rules

vtuner-dmm-3.o: ../../vtuner-dmm-3.c ../../vtuner-dmm-3.h 
	$(CC) $(CFLAGS) -c -o vtuner-dmm-3.o ../../vtuner-dmm-3.c

vtunerd-plugin.o: ../../vtunerd-plugin.c ../../vtunerd-service.h ../../vtuner-utils.h
	$(CXX) $(CFLAGS) $(PLUGIN_CFLAGS-$(ARCH)) -c -o vtunerd-plugin.o ../../vtunerd-plugin.c
