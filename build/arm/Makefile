ARCH=arm
-include ../../Make.config

CC=$(CC-$(ARCH))
CXX=$(CXX-$(ARCH))
CFLAGS=-fpic -DHAVE_DVB_API_VERSION=5
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dvb-3
STRIP=$(STRIP-$(ARCH))

all: ../../dist/$(ARCH)/vtunerd.$(ARCH) 
	$(STRIP) ../../dist/$(ARCH)/vtunerd.$(ARCH) 
	
-include ../../Make.rules
	
vtuner-dvb-3.o: ../../vtuner-dvb-3.c ../../vtuner-dvb-3.h 
	$(CC) $(CFLAGS) -c -o vtuner-dvb-3.o ../../vtuner-dvb-3.c

