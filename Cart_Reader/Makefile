#!/usr/bin/make -f
# an altertantive to this simple approach may be: https://github.com/bxparks/EpoxyDuino

CC=gcc
CFLAGS=-include retread.h -DENABLE_MD -DENABLE_SNES -DENABLE_NES

.SUFFIXES : .o .c .cpp .ino

%.o: %.ino
	$(CC) -c $(CFLAGS) -x c++ $< -o $*.o

INO_SOURCES := $(wildcard *.ino)

all: Cart_Reader

clean:
	rm *.o MD SNES Cart_Reader

# todo
# compile .ino suffixes as C code with special patches
# maybe use #ifndef RETRODE3 to keep original code and #else for Retrode3 code?
#   so that we can directly patch upstream?
# convert or provide uint16_t for "word", uint8_t for "byte"
# handle read/write through file descriptor
# remove all access to PORTA, PORTC, PORTF, etc
# wrap or remove menu handling


SNES: MD.o

SNES: SNES.o

SNES: NES.o

Cart_Reader: Cart_Reader.o SNES.o MD.o NES.o
