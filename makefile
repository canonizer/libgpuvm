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
TGT_SO=lib$(NAME).so
TGT_WITH_VERSION=$(TGT_SO).$(VERSION)
TGT=bin/$(TGT_WITH_VERSION)
TGT_HEADER=$(NAME).h
TMP=$(TGT) *~ src/*~ $(TGT) doc/*/*

# compilation settings
CC=gcc
CFLAGS=-O2
SOFLAGS=-fPIC -fvisibility=hidden -shared -Wl,-soname,$(TGT_SO).$(MAJOR_VERSION)
LIBS=-lOpenCL -lpthread

build : $(TGT)

$(TGT) : $(SRC)
	$(CC) $(CFLAGS) $(SOFLAGS) $(LIBS) $(SRCC) -o $(TGT)

# todo: handle setting symbolic links in a more accurate way
install:	$(TGT)
	cp $(TGT) $(LIB_PREFIX)
	cp $(TGT_HEADER) $(HEADER_PREFIX)
	ln -sf $(PREFIX)/$(TGT_WITH_VERSION) $(PREFIX)/$(TGT_SO).$(MAJOR_VERSION)
	ln -sf $(PREFIX)/$(TGT_SO).$(MAJOR_VERSION) $(PREFIX)/$(TGT_SO)

doxy:	$(SRC)
	doxygen doxygen

clean:	
	rm -rf $(TMP)
