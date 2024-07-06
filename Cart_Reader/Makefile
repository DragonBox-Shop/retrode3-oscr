#!/usr/bin/make -f
# an alternative to this simple approach may be: https://github.com/bxparks/EpoxyDuino
# but it is difficult to manage the hardware access

# may need "apt-get install g++"
# you can specify a cross-compiler here
CC=g++

CFLAGS=

.SUFFIXES : .o .c .cpp .ino
.PHONY: all clean remote

INO_SOURCES := $(wildcard *.ino)
INO_SOURCES := Cart_Reader.ino SNES.ino MD.ino NES.ino

all: retread

%.o: %.ino	# directly compile .ino using C++
	$(CC) -c $(CFLAGS) -include retread.h -D__Linux__ include retread.h -include ino.h -x c++ $< -o $*.o

%.o: %.cpp
	$(CC) -c $(CFLAGS) -D__Linux__ -include retread.h -x c++ -std=c++11 $< -o $*.o

%.o: %.c
	$(CC) -c $(CFLAGS) -D__Linux__ $< -o $*.o

ino.cpp: $(INO_SOURCES)	# collect all .ino source files into a single one and keep source file references
	( echo "#include \"ino.h\""; for FILE in $^; do echo "# 1 \"$$FILE\""; cat "$$FILE"; done ) >$@

ino.h: ino.cpp	# derive forward declarations
	sed -n '/^[a-zA-Z_0-9].*(.*).*{.*$$/s/{/;/p' $^ | sed 's/= [^,)]*[^,)]//g' | sed 's/\}//g' >ino.h

clean:
	rm -f *.o ino.* retread

DIR=/root/retrode3/Cart_Reader
DEVICE=root@192.168.0.202
remote: clean
	rsync -rltDvzh "./" "$(DEVICE):$(DIR)"
	ssh $(DEVICE) sh -c "cd; uname -a; cd $(DIR); make clean all"

OSCR.o: OSCR.h OSCR.cpp

ino.o: ino.h $(INO_SOURCES)

retread.o: retread.h retread.cpp

# command line tool
retread: retread.o OSCR.o ino.o
