# configuration settings - specifying OS
OSNAME:=$(shell uname -s)
ARCH:=$(shell uname -m)

include makefile.def

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
TGT_WITH_MAJOR_VERSION=$(TGT_DL).$(MAJOR_VERSION)
ifeq ($(OSNAME), Darwin)
	TGT_WITH_VERSION=lib$(NAME).$(VERSION).$(DL_SUFFIX)
	TGT_WITH_MAJOR_VERSION=lib$(NAME).$(MAJOR_VERSION).$(DL_SUFFIX)
endif
TGT=bin/$(TGT_WITH_VERSION)
HEADER=$(NAME).h
TGT_HEADER=src/$(HEADER)
TMP=$(TGT) *~ src/*~ $(TGT) bin/$(TGT_WITH_MAJOR_VERSION) bin/*.$(DL_SUFFIX) \
	doc/*/* samples/bin/* samples/*/*~ samples/*/src/*~

# compilation settings, also handle configuration and OS-dependent settings
INCLUDE_DIRS=
CC=gcc
CFLAGS=-O2
DEFS=
DL_FLAGS=-fPIC -fvisibility=hidden -shared -Wl,-soname,$(TGT_DL).$(MAJOR_VERSION)
LIBS=-lpthread
LIB_DIRS=
LIBDIR_STD=lib
ifeq ($(ARCH), x86_64)
	LIBDIR_STD=lib64
endif
ifeq ($(ARCH), amd64)
  LIBDIR_STD=lib64
endif
ifeq ($(ENABLE_OPENCL), y)
	DEFS+= -DOPENCL_ENABLED
	LIBS+= -lOpenCL
endif
ifeq ($(ENABLE_CUDA), y)
	DEFS+= -DCUDA_ENABLED
	LIBS+= -lcudart
	INCLUDE_DIRS+= -I$(CUDA_INSTALL_PATH)/include
	LIB_DIRS+= -L$(CUDA_INSTALL_PATH)/$(LIBDIR_STD)
endif
ifeq ($(OSNAME), Darwin)
	LIBS+= -lrt
	INCLUDE_DIRS+= -I/system/library/frameworks/opencl.framework/headers
	DL_FLAGS=-fvisibility=hidden -dynamiclib
	CFLAGS+= -arch i386 -arch x86_64
endif

build : $(TGT)

$(TGT) : $(SRC) makefile.def
	$(CC) $(CFLAGS) $(DL_FLAGS) $(DEFS) $(INCLUDE_DIRS) $(LIB_DIRS) $(LIBS) \
		$(SRCC) -o $(TGT)
	ln -sf $(TGT_WITH_VERSION) bin/$(TGT_DL)
	ln -sf $(TGT_WITH_VERSION) bin/$(TGT_WITH_MAJOR_VERSION)

# todo: handle setting symbolic links in a more accurate way
install:	$(TGT)
	cp $(TGT_HEADER) $(HEADER_PREFIX)
	cp $(TGT) $(LIB_PREFIX)
	ln -sf $(LIB_PREFIX)/$(TGT_WITH_VERSION) $(LIB_PREFIX)/$(TGT_WITH_MAJOR_VERSION)
	ln -sf $(LIB_PREFIX)/$(TGT_WITH_MAJOR_VERSION) $(LIB_PREFIX)/$(TGT_DL)

uninstall:
	rm -f $(LIB_PREFIX)/$(TGT_DL)
	rm -f $(LIB_PREFIX)/$(TGT_WITH_MAJOR_VERSION)
	rm -f $(LIB_PREFIX)/$(TGT_WITH_VERSION)
	rm -f $(LIB_PREFIX)/$(HEADER)

doxy:	$(SRC)
	doxygen doxygen

clean:	
	rm -rf $(TMP) 
