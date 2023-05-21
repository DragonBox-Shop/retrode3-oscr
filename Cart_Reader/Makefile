#!/usr/bin/make -f
# an altertantive to this simple approach may be: https://github.com/bxparks/EpoxyDuino

.SUFFIXES : .o .c .cpp .ino

%.o: %.ino
	$(CC) -c $(OBJCFLAGS) -x c++ $< -o $*.o

INO_SOURCES := $(wildcard *.ino)

all: MD Cart_Reader

clean:

# todo
# compile .ino suffixes as C code with special patches
# maybe use #ifndef RETRODE3 to keep original code and #else for Retrode3 code?
#   so that we can directly patch upstream?
# convert or provide uint16_t for "word", uint8_t for "byte"
# handle read/write through file descriptor
# remove all access to PORTA, PORTC, PORTF, etc
# wrap or remove menu handling


MD: MD.o

Cart_Reader: Cart_Reader.o
