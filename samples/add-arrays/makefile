NAME=hello-world
TGT=bin/$(NAME)
SRC_C=src/*.c
SRC=src/*.c src/*.h
TMP=*~ src/*~

build: $(TGT)
$(TGT):	$(SRC)
	gcc -std=c99 -L../../bin -I../../src -lgpuvm -lOpenCL $(SRC_C) -o$(TGT)

run: $(TGT)
	$(TGT)

clean:
	rm -f $(TGT) $(TMP)
