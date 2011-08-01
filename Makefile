-include ../../Make.config

all: i686 x86_64 mipsel ppc db2 sh4 mipsel15 ipkg vtunerc_driver

i686:
	$(MAKE) -C build/i686 all
	
x86_64:
	$(MAKE) -C build/x86_64 all
	
ppc: 	
	$(MAKE) -C build/ppc

db2: 	
	$(MAKE) -C build/db2

mipsel: 	
	$(MAKE) -C build/mipsel all

mipsel15:         
	$(MAKE) -C build/mipsel15
	
sh4:         
	$(MAKE) -C build/sh4

ipkg:   mipsel
	$(MAKE) -C pkgs ipkg
    	
arm:         
	$(MAKE) -C build/arm

vtunerc_driver:
	$(MAKE) -C driver/vtunerc
	
clean:
	$(MAKE) -C build/i686 clean
	$(MAKE) -C build/x86_64 clean
	$(MAKE) -C build/ppc clean
	$(MAKE) -C build/db2 clean
	$(MAKE) -C build/mipsel clean
	$(MAKE) -C build/sh4 clean
	$(MAKE) -C build/mipsel15 clean
	$(MAKE) -C pkgs clean
	$(MAKE) -C build/arm clean
	$(MAKE) -C driver/vtunerc clean
