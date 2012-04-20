ifeq ($(CC), cc)
	CC=gcc
endif
ifndef STDEXT
	STDEXT=c
endif
TGT=../bin/$(NAME)
SRC_C=src/*.$(STDEXT)
SRC=$(SRC_C)
TMP=*~ src/*~ $(TGT)
#INCLUDE_DIRS += -I../../src
LIB_DIRS += -L../../bin
OSNAME:=$(shell uname -s)
CFLAGS = -O2
ifeq ($(CC), gcc)
	CFLAGS += -std=gnu99 -pthread
endif
LIBS += -lgpuvm -lOpenCL
ifeq ($(OSNAME), Darwin)
  INCLUDE_DIRS += -I/system/library/frameworks/opencl.framework/headers
endif

build: $(TGT)
$(TGT):	$(SRC)
	$(CC) $(CFLAGS) $(LIB_DIRS) $(INCLUDE_DIRS) $(LIBS) $(SRC_C) -o $(TGT)

run: $(TGT)
	$(TGT)

clean:
	rm -f $(TMP)
