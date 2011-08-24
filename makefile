# configuration settings - specifying OS
OSNAME:=$(shell uname -s)

# installation settings
PREFIX=/usr
# TODO: handle 64-bit case
LIB_PREFIX=$(PREFIX)/lib
HEADER_PREFIX=$(PREFIX)/include

# project variables
NAME=gpuvm
MAJOR_VERSION=0
MINOR_VERSION=0.1
VERSION=$(MAJOR_VERSION).$(MINOR_VERSION)
SRCH=src/*.h
SRCC=src/*.c
SRC=$(SRCH) $(SRCC)
DL_SUFFIX=so
ifeq ($(OSNAME), Darwin)
	DL_SUFFIX=dylib
endif
TGT_DL=lib$(NAME).$(DL_SUFFIX)
TGT_WITH_VERSION=$(TGT_DL).$(VERSION)
ifeq ($(OSNAME), Darwin)
	TGT_WITH_VERSION=lib$(NAME).$(VERSION).$(DL_SUFFIX)
endif
TGT=bin/$(TGT_WITH_VERSION)
TGT_HEADER=$(NAME).h
TMP=$(TGT) *~ src/*~ $(TGT) bin/*.$(DL_SUFFIX) doc/*/*

# compilation settings
INCLUDE_DIRS=
ifeq ($(OSNAME), Darwin)
	INCLUDE_DIRS=-I/system/library/frameworks/opencl.framework/headers
endif
CC=gcc
CFLAGS=-O2
DL_FLAGS=-fPIC -fvisibility=hidden -shared -Wl,-soname,$(TGT_DL).$(MAJOR_VERSION)
ifeq ($(OSNAME), Darwin)
	DL_FLAGS=-fvisibility=hidden -dynamiclib
# mono is 32-bit on Mac OS X, so change the 'bitness' accordingly
	CFLAGS+= -m32
endif
LIBS=-lOpenCL -lpthread

build : $(TGT)

$(TGT) : $(SRC)
	$(CC) $(CFLAGS) $(DL_FLAGS) $(INCLUDE_DIRS) $(LIBS) $(SRCC) -o $(TGT)
	ln -sf $(TGT_WITH_VERSION) bin/$(TGT_DL)

# todo: handle setting symbolic links in a more accurate way
install:	$(TGT)
	cp $(TGT) $(LIB_PREFIX)
	cp $(TGT_HEADER) $(HEADER_PREFIX)
	ln -sf $(PREFIX)/$(TGT_WITH_VERSION) $(PREFIX)/$(TGT_DL).$(MAJOR_VERSION)
	ln -sf $(PREFIX)/$(TGT_DL).$(MAJOR_VERSION) $(PREFIX)/$(TGT_DL)

doxy:	$(SRC)
	doxygen doxygen

clean:	
	rm -rf $(TMP)
