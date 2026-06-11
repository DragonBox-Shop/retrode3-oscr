#!/usr/bin/make -f
# (C) by H. N. Schaller (hns@goldelico.com) - licenced under GPL V3
#
# a Command Line Tool "oscr" as a wrapper around the OSCR source code
# internally uses the retrode3-lib to access the slots
#

CC        = gcc
CXX       = g++
CFLAGS    = -O2	# -Wall
CXXFLAGS  = -O2 # -Wall
LDFLAGS   =
LIBS      =

# call e.g. make clean all ARCH=mipsel

ifdef ARCH
QSTEP	  = ~/QuantumSTEP/System/Library/Frameworks/System.framework/Versions/Current/8-Jessie/$(ARCH)/usr/bin
CC        = $(QSTEP)/$(ARCH)-linux-gnu-gcc
CXX       = $(QSTEP)/$(ARCH)-linux-gnu-g++
LDFLAGS   += -static
LIBS      += -lgcc -lgcc_eh -Wl,--defsym=__gcc_personality_v0=0
endif

OSCR_TARGET := oscr
RLIB_TARGET := retrode3-lib.o
RLIB_HEADER := retrode3-lib.h
OSCR_OBJS := $(RLIB_TARGET) oscr-cmd.o all-ino.o

.SUFFIXES : .o .c .cpp .ino
.PHONY: all clean install remote

#INO_SOURCES := $(wildcard Cart_Reader/*.ino) Cart_Reader/OSCR.cpp
INO_SOURCES := Cart_Reader/Cart_Reader.ino Cart_Reader/SNES.ino Cart_Reader/MD.ino Cart_Reader/FLASH.ino Cart_Reader/NES.ino Cart_Reader/OSCR.cpp

all: build-tools $(OSCR_TARGET)

build-tools:	# this installs the g++ package if g++ is missing
	@echo '*** checking/installing $(CXX) ***'
	@if [ "`which $(CXX)`" ]; \
	then echo "$(CXX) is available as `which $(CXX)`".; \
	else apt-get update && yes | apt-get -y --force-yes install g++; \
	fi

%.o: %.ino	# directly compile .ino using C++
	$(CXX) -c $(CFLAGS) -I Cart_Reader/ -include oscr-cmd.h -include all-ino.h -x c++ $< -o $*.o

%.o: %.cpp
	$(CXX) -c $(CFLAGS) -I Cart_Reader/ -include oscr-cmd.h -x c++ -std=c++11 $< -o $*.o

%.o: %.c
	$(CXX) -c $(CFLAGS) -I Cart_Reader/ -x c $< -o $*.o

all-ino.cpp: $(INO_SOURCES)	# collect all .ino source files into a single one and keep source file references --- FIXME: maybe we should preprocess first?
	( echo "#include \"all-ino.h\""; for FILE in $^; do echo "# 1 \"$$FILE\""; cat "$$FILE"; done ) >$@

all-ino.h: all-ino.cpp	# derive forward declarations
	( cat Cart_Reader/OSCR.h; sed -n '/^[a-zA-Z_0-9].*(.*).*{.*$$/s/{/;/p' $^ | sed 's/= [^,)]*[^,)]//g' | sed 's/\}//g' ) >all-ino.h

clean:
	@echo '*** make clean ***'
	rm -f *.o all-ino.* $(OSCR_TARGET)

all-ino.o: oscr-cmd.h all-ino.h all-ino.cpp

# NOTE: this is not built again on build host if we had run a remote build which copies the MIPS file from there...
retrode-lib: retrode3-lib.o $(RLIB_HEADER)
	@ls -l $(RLIB_TARGET)
	@file $(RLIB_TARGET)

oscr-cmd.o: oscr-cmd.h all-ino.h oscr-cmd.cpp

# link command line tool
$(OSCR_TARGET): retrode-lib $(OSCR_OBJS)
	$(CXX) $(LDFLAGS) $(OSCR_OBJS) -o $(OSCR_TARGET) $(LIBS)
	@ls -l $(OSCR_TARGET)
	@file $(OSCR_TARGET)

install: $(OSCR_TARGET)
	@echo '*** make install ***'
	cp oscr $(BINDIR)/
	mkdir -p $(BINDIR)/../man/man1
	gzip <oscr.groff >$(BINDIR)/../man/man1/oscr.1.gz
	mkdir -p $(BINDIR)/../lib $(BINDIR)/../include $(BINDIR)/../man/man3
	cp $(RLIB_TARGET) $(BINDIR)/../lib/
	cp $(RLIB_HEADER) $(BINDIR)/../include/
	gzip <retrode3-lib.groff >$(BINDIR)/../man/man3/retrode3-lib.3.gz

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
	scp "$(DEVICE):$(SRCDIR)/$(OSCR_TARGET)" "$(DEVICE):$(SRCDIR)/$(RLIB_TARGET)" .	# pull binaries from device
