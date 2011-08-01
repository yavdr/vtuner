ARCH=db2
-include ../../Make.config

CC=$(CC-$(ARCH))
CFLAGS=-m32 -DHAVE_DVB_API_VERSION=3 $(DBGFLAGS)
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dvb-3
-include ../../Make.rules

vtuner-dvb-3.o: ../../vtuner-dvb-3.c ../../vtuner-dvb-3.h 
	$(CC) $(CFLAGS) -c -o vtuner-dvb-3.o ../../vtuner-dvb-3.c

all: vtunerd-$(ARCH)

	
