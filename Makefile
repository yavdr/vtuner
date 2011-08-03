DBG_LEVEL ?= 0x000f
USE_SYSLOG ?= 1
CFLAGS=-DHAVE_DVB_API_VERSION=5 -g -fPIC -O2 -DDBG_LEVEL=$(DBG_LEVEL) -DUSE_SYSLOG=$(USE_SYSLOG)
LDFLAGS=-lpthread -lrt
DRIVER=vtuner-dvb-3
VERSION ?= 0.0.0

all: vtunerd vtunerc driver/vtunerc/dkms.conf

vtunerd: vtunerd.c vtunerd-service.o vtuner-network.o vtuner-utils.o $(DRIVER).o
	$(CC) $(CFLAGS) $(LDFLAGS) -o vtunerd vtuner-network.o vtunerd-service.o $(DRIVER).o vtuner-utils.o vtunerd.c

vtunerc: vtunerc.c vtuner-network.o vtuner-utils.o
	$(CC) $(CFLAGS) -o vtunerc vtuner-network.o vtuner-utils.o vtunerc.c $(LDFLAGS)

vtunerd-service.o: vtunerd-service.c vtunerd-service.h
	$(CC) $(CFLAGS) -c -o vtunerd-service.o vtunerd-service.c
	
vtuner-network.o: vtuner-network.c vtuner-network.h
	$(CC) $(CFLAGS) -c -o vtuner-network.o vtuner-network.c
	
vtuner-utils.o: vtuner-utils.c vtuner-utils.h
	$(CC) $(CFLAGS) -c -o vtuner-utils.o vtuner-utils.c
	
vtuner-dvb-3.o: vtuner-dvb-3.c vtuner-dvb-3.h                                                               
	$(CC) $(CFLAGS) -c -o vtuner-dvb-3.o vtuner-dvb-3.c                                                       
                                                                                                                        
driver/vtunerc/dkms.conf: driver/vtunerc/dkms.conf.in
	sed -e 's/#VERSION#/${VERSION}/g' <driver/vtunerc/dkms.conf.in >driver/vtunerc/dkms.conf
                                              
clean:
	rm -rf *.o *.so
	rm -f vtunerd
	rm -f vtunerc
	rm -f driver/vtunerc/dkms.conf

install:
	install -m 755 -D vtunerd $(DESTDIR)/usr/bin/vtunerd
	install -m 755 -D vtunerc $(DESTDIR)/usr/bin/vtunerc
	install -d $(DESTDIR)/usr/src
	cp -a driver/vtunerc $(DESTDIR)/usr/src/vtunerc-${VERSION}
	rm -f $(DESTDIR)/usr/src/vtunerc-${VERSION}/dkms.conf.in
