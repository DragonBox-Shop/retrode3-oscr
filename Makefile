#!/usr/bin/make -f
# (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3
#
# a Command Line Tool "oscr" as a wrapper around the OSCR source code
# internally uses the retrode-lib to access the slots
#

# you can also specify a cross-compiler here
CC=g++
CFLAGS=

.SUFFIXES : .o .c .cpp .ino
.PHONY: all clean install remote

#INO_SOURCES := $(wildcard Cart_Reader/*.ino) Cart_Reader/OSCR.cpp
INO_SOURCES := Cart_Reader/Cart_Reader.ino Cart_Reader/SNES.ino Cart_Reader/MD.ino Cart_Reader/FLASH.ino Cart_Reader/NES.ino Cart_Reader/OSCR.cpp

all: build-tools oscr

build-tools:	# this installs the g++ package if g++ is missing
	@echo '*** checking/installing g++ ***'
	@if [ "`which g++`" ]; \
	then echo g++ is available.; \
	else apt-get update && yes | apt-get -y --force-yes install g++; \
	fi

%.o: %.ino	# directly compile .ino using C++
	$(CC) -c $(CFLAGS) -I Cart_Reader/ -include oscr-cmd.h -include all-ino.h -x c++ $< -o $*.o

%.o: %.cpp
	$(CC) -c $(CFLAGS) -I Cart_Reader/ -include oscr-cmd.h -x c++ -std=c++11 $< -o $*.o

%.o: %.c
	$(CC) -c $(CFLAGS) -I Cart_Reader/ -x c $< -o $*.o

all-ino.cpp: $(INO_SOURCES)	# collect all .ino source files into a single one and keep source file references --- FIXME: maybe we should preprocess first?
	( echo "#include \"all-ino.h\""; for FILE in $^; do echo "# 1 \"$$FILE\""; cat "$$FILE"; done ) >$@

all-ino.h: all-ino.cpp	# derive forward declarations
	( cat Cart_Reader/OSCR.h; sed -n '/^[a-zA-Z_0-9].*(.*).*{.*$$/s/{/;/p' $^ | sed 's/= [^,)]*[^,)]//g' | sed 's/\}//g' ) >all-ino.h

clean:
	@echo '*** make clean ***'
	rm -f *.o all-ino.* oscr

all-ino.o: oscr-cmd.h all-ino.h all-ino.cpp

# NOTE: this is not built again on build host if we had run a remote build which copies the MIPS file from there...
retrode-lib.o: retrode-lib.c retrode-lib.h

oscr-cmd.o: oscr-cmd.h all-ino.h oscr-cmd.cpp

# link command line tool
oscr: retrode-lib.o oscr-cmd.o all-ino.o
	$(CC) $(CFLAGS) oscr-cmd.o all-ino.o retrode-lib.o -o oscr

install: oscr
	@echo '*** make install ***'
	cp oscr $(BINDIR)/
	mkdir -p $(BINDIR)/../man/man1
	gzip <oscr.groff >$(BINDIR)/../man/man1/oscr.1.gz
	mkdir -p $(BINDIR)/../lib $(BINDIR)/../include $(BINDIR)/../man/man3
	cp retrode-lib.o $(BINDIR)/../lib/
	cp retrode-lib.h $(BINDIR)/../include/
	gzip <retrode-lib.groff >$(BINDIR)/../man/man3/retrode-lib.3.gz

# this allows to run "make remote" on the connected PC

DEVICE=root@192.168.0.202
SRCDIR=/usr/local/src/oscr
BINDIR=/usr/local/bin/
ROOTDIR=/usr/local/games/oscr/

remote: clean
	@echo '*** make remote ***'
	ssh $(DEVICE) sh -c "cd; mkdir -p $(SRCDIR) $(ROOTDIR)"
	rsync -rltDvzh --exclude .git Makefile "./Cart_Reader" *.h *.c *.cpp *.groff "$(DEVICE):$(SRCDIR)"
	rsync -rltDvzh "sd/" "$(DEVICE):$(ROOTDIR)/"
	ssh $(DEVICE) sh -c "cd; uname -a; cd $(SRCDIR) && make clean all install"
	scp "$(DEVICE):$(SRCDIR)/oscr" "$(DEVICE):$(SRCDIR)/retrode-lib.o" .	# pull binaries from device
