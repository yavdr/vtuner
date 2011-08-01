ARCH=ppc
-include ../../Make.config

CC=$(CC-$(ARCH))
CFLAGS=-DHAVE_DREAMBOX_HARDWARE $(INCLUDE-ppc) $(DBGFLAGS)
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dmm-2
-include ../../Make.rules

vtuner-dmm-2.o: ../../vtuner-dmm-2.c ../../vtuner-dmm-2.h 
	$(CC) $(CFLAGS) -c -o vtuner-dmm-2.o ../../vtuner-dmm-2.c

all: vtunerd-$(ARCH)