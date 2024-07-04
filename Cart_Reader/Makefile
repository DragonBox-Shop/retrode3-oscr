#!/usr/bin/make -f
# an alternative to this simple approach may be: https://github.com/bxparks/EpoxyDuino
# but it is difficult to manage the hardware access

CC=gcc
CFLAGS=-include retread.h -DENABLE_MD -DENABLE_SNES -DENABLE_NES

.SUFFIXES : .o .c .cpp .ino

%.o: %.ino
	$(CC) -c $(CFLAGS) -x c++ $< -o $*.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $*.o

# INO_SOURCES := $(wildcard *.ino)

all: Cart_Reader

clean:
	rm -f *.o MD SNES NES Cart_Reader retread

MD: MD.o

SNES: SNES.o

NES: NES.o

retread: retread.o SNES.o MD.o NES.o

Cart_Reader: Cart_Reader.o SNES.o MD.o NES.o
