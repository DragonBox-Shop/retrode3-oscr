#!/usr/bin/make -f
# an alternative to this simple approach may be: https://github.com/bxparks/EpoxyDuino
# but it is difficult to manage the hardware access

# specify a cross-compiler here
CC=gcc

CFLAGS=-include retread.h -D__Linux__

.SUFFIXES : .o .c .cpp .ino

%.o: %.ino
	sed -n '/^[a-zA-Z_0-9].*(.*).*{.*$$/s/{/;/p' $< >.$*.h	# derive forward declarations
	$(CC) -c $(CFLAGS) -include .$*.h -x c++ $< -o $*.o	# compile .ino using C++

%.o: %.cpp
	$(CC) -c $(CFLAGS) -x c++ $< -o $*.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $*.o

all: retread

clean:
	rm -f *.o .*.h retread

# debugging targets
MD: MD.o
SNES: SNES.o
NES: NES.o

# command line tool
retread: retread.o SNES.o MD.o NES.o
