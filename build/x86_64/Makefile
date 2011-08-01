ARCH=x86_64
-include ../../Make.config

CC=$(CC-$(ARCH))
CFLAGS=-m64 -DHAVE_DVB_API_VERSION=5 $(INCLUDE-x86_64) $(DBGFLAGS)
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dvb-3
-include ../../Make.rules

vtuner-dvb-3.o: ../../vtuner-dvb-3.c ../../vtuner-dvb-3.h 
	$(CC) $(CFLAGS) -c -o vtuner-dvb-3.o ../../vtuner-dvb-3.c

all: ../../dist/$(ARCH)/vtunerd.$(ARCH) ../../dist/$(ARCH)/vtunerc.$(ARCH)
