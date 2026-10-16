# PS3 File Manager (PSL1GHT)
# Wymaga: ps3toolchain + PSL1GHT (zmienne PS3DEV i PSL1GHT)
ifeq ($(strip $(PSL1GHT)),)
$(error "Ustaw zmienną PSL1GHT")
endif
include $(PSL1GHT)/ppu_rules

TARGET    := ps3-filemanager
TITLE     := PS3 File Manager
APPID     := FMGR00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
SFOXML    ?= $(PSL1GHT)/bin/sfo.xml
PKGDIR    := pkgdir

CC      := ppu-gcc
SRC     := $(wildcard *.c)
OBJ     := $(SRC:.c=.o)
CFLAGS  := -O2 -Wall -mcpu=cell $(MACHDEP) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
LDFLAGS := $(MACHDEP) -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib
LIBS    := -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

.PHONY: all pkg clean
all: $(TARGET).self

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).self: $(TARGET).elf
	$(PSL1GHT)/bin/sprxlinker $<
	$(PSL1GHT)/bin/make_self $< $@

pkg: $(TARGET).elf
	mkdir -p $(PKGDIR)/USRDIR
	$(PSL1GHT)/bin/sprxlinker $<
	$(PSL1GHT)/bin/make_self_npdrm $< $(PKGDIR)/USRDIR/EBOOT.BIN $(CONTENTID)
	python3 $(PSL1GHT)/bin/sfo.py --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(PKGDIR)/PARAM.SFO
	python3 $(PSL1GHT)/bin/pkg.py --contentid $(CONTENTID) $(PKGDIR)/ $(TARGET).pkg

clean:
	rm -rf *.o *.elf *.self *.pkg $(PKGDIR)
