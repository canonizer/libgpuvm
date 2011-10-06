TGT=../bin/$(NAME)
SRC_C=src/*.c
SRC=src/*.c src/*.h
TMP=*~ src/*~ $(TGT)
#INCLUDE_DIRS += -I../../src
LIB_DIRS += -L../../bin
OSNAME:=$(shell uname -s)
CFLAGS += -std=c99 -O2
LIBS += -lgpuvm -lOpenCL
ifeq ($(OSNAME), Darwin)
  INCLUDE_DIRS += -I/system/library/frameworks/opencl.framework/headers
#  CFLAGS += -m32
endif

build: $(TGT)
$(TGT):	$(SRC)
	gcc $(CFLAGS) $(LIB_DIRS) $(INCLUDE_DIRS) $(LIBS) $(SRC_C) -o $(TGT)

run: $(TGT)
	$(TGT)

clean:
	rm -f $(TMP)
