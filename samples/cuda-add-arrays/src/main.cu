/** CUDA sample: adding two arrays with libgpuvm */

#include <cuda.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/types.h>

extern "C" {
  #include "../../../src/gpuvm.h"
}

// macros to check for errors
#define CHECK(x) \
	{\
		int res = (x);															\
		if(res != cudaSuccess) {										\
			printf(#x "\n");													\
			printf("%d\n", res);											\
			exit(-1);																	\
		}																						\
	}

#define CHECK_NULL(x) \
	if(x == NULL) {\
	printf(#x "\n");\
	exit(-1);\
	}

#define COUNT 4

#define N (1024 * 13 + 64)
#define SZ (N * sizeof(int))
#define NRUNS 1

void __global__ add_arrays_kernel(int *c, int *a, int *b, int n) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	c[i] = a[i] + b[i];
}

/** adds arrays on GPU, by calling OpenCL kernel; note that device pointers
		which correspond to host pointers are obtained using gpuvm_xlate(), and are
		not passed separately to the function */
void add_arrays_on_gpu(int *c, int *a, int *b, int n) {
	CHECK(gpuvm_kernel_begin(a, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(b, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(c, 0, GPUVM_READ_WRITE));

	add_arrays_kernel<<<n/64, 64>>>
		((int*)gpuvm_xlate(c, 0), (int*)gpuvm_xlate(a, 0), (int*)gpuvm_xlate(b, 0), n);

	CHECK(cudaDeviceSynchronize());

	// on kernel end
	//printf("actions on kernel end\n");
	CHECK(gpuvm_kernel_end(a, 0));
	CHECK(gpuvm_kernel_end(b, 0));
	CHECK(gpuvm_kernel_end(c, 0));
	// CHECK(cudaMemcpy(a, gpuvm_xlate(a, 0), n * sizeof(int), cudaMemcpyDeviceToHost));
	// CHECK(cudaMemcpy(b, gpuvm_xlate(b, 0), n * sizeof(int), cudaMemcpyDeviceToHost));
	// CHECK(cudaMemcpy(c, gpuvm_xlate(c, 0), n * sizeof(int), cudaMemcpyDeviceToHost));
	// CHECK(cudaDeviceSynchronize());
}

int main(int argc, char** argv) {
	
	//CHECK(gpuvm_pre_init(GPUVM_THREADS_BEFORE_INIT));
	//CHECK(gpuvm_pre_init(GPUVM_THREADS_AFTER_INIT));

	// initialize GPUVM
	CHECK(gpuvm_init
				(1, 0, GPUVM_CUDA | GPUVM_UNLINK_NO_SYNC_BACK | GPUVM_WRITER_SIG_BLOCK));

	// allocate host data
	int *ha = 0, *hb = 0, *hc = 0, *hg = 0;
	ha = (int*)malloc(SZ);
	hb = (int*)malloc(SZ);
	hc = (int*)malloc(SZ);
	hg = (int*)malloc(SZ);
	CHECK_NULL(ha);
	CHECK_NULL(hb);
	CHECK_NULL(hg);
	for(int i = 0; i < N; i++) {
		ha[i] = i;
		hb[i] = i + 1;
	}

	// allocate device data
	int *da, *db, *dc;
	CHECK(cudaMalloc(&da, SZ));
	CHECK(cudaMalloc(&db, SZ));
	CHECK(cudaMalloc(&dc, SZ));

	// link host buffers to device buffers
	//printf("linking buffers\n");
	CHECK(gpuvm_link(ha, SZ, 0, da, GPUVM_ON_HOST));
	CHECK(gpuvm_link(hb, SZ, 0, db, GPUVM_ON_HOST));
	CHECK(gpuvm_link(hc, SZ, 0, dc, GPUVM_ON_HOST));

	// before-kernel actions
	printf("adding arrays\n");

	unsigned irun;
	for(irun = 0; irun < NRUNS; irun++) {
		// do work on GPU
		add_arrays_on_gpu(hc, ha, hb, N);

		// evaluate "gold" result
		for(int i = 0; i < N; i++)
			hg[i] = ha[i] + hb[i];

		// check result
		for(int i = 0; i < N; i++) {
			if(hg[i] != hc[i]) {
				printf("check: FAILED\n");
				printf("hg[%d] != hc[%d]: %d != %d\n", i, i, hg[i], hc[i]);
				exit(-1);
			}
		}
		printf("check: PASSED\n");
	}  // for(irun)

	// print result
	printf("printing result\n");
	int step = 1536;
	for(int i = 0; i < N; i += step)
		printf("hc[%d] = %d\n", i, hc[i]);

	// unlink
	CHECK(gpuvm_unlink(ha, 0));
	CHECK(gpuvm_unlink(hb, 0));
	CHECK(gpuvm_unlink(hc, 0));

	// free CUDA device arrays
	cudaFree(da);
	cudaFree(db);
	cudaFree(dc);

	// free host memory
	free(ha);
	free(hb);
	free(hc);
	free(hg);

	return 0;
}  // end of main()
