/** OpenCL "hello, world!" sample; adding two arrays */

#define _GNU_SOURCE

#ifdef __APPLE__
  #include <cl.h>
#else
  #include <CL/cl.h>
#endif
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/types.h>

#include "../../../src/gpuvm.h"
#include "helper.h"

// macros to check for errors
#define CHECK(x) \
	{\
	cl_int res = x;\
	if(res != CL_SUCCESS) {\
	printf(#x "\n");\
	printf("%d\n", res);\
	exit(-1);\
	}\
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

cl_command_queue queue;
cl_kernel add_arrays_kernel;

void get_device(cl_device_id *pdev) {

	// get platform
	cl_platform_id platform;
	CHECK(clGetPlatformIDs(1, &platform, 0));

	//print_dls();

	// get device
	int ndevs = 0;
	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, pdev, &ndevs);

	if(ndevs)
		return;
	else {
		printf("can\'t get OpenCL device\n");
		exit(-1);
	}
}  // get_device

/** adds arrays on GPU, by calling OpenCL kernel; note that device pointers
		which correspond to host pointers are obtained using gpuvm_xlate(), and are
		not passed separately to the function */
void add_arrays_on_gpu(int *c, int *a, int *b, int n) {
	CHECK(gpuvm_kernel_begin(a, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(b, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(c, 0, GPUVM_READ_WRITE));

	cl_mem dc_arg = gpuvm_xlate(c, 0);
	cl_mem da_arg = gpuvm_xlate(b, 0);
	cl_mem db_arg = gpuvm_xlate(a, 0);
	
	// run program
	CHECK(clSetKernelArg(add_arrays_kernel, 0, sizeof(cl_mem), &dc_arg));
	CHECK(clSetKernelArg(add_arrays_kernel, 1, sizeof(cl_mem), &da_arg));
	CHECK(clSetKernelArg(add_arrays_kernel, 2, sizeof(cl_mem), &db_arg));
	size_t gws[1] = {n};
	size_t lws[1] = {64};
	size_t gwos[1] = {0};
	CHECK(clEnqueueNDRangeKernel(queue, add_arrays_kernel, 1, gwos, 
															 gws, lws, 0, 0, 0));
	CHECK(clFinish(queue));

	// on kernel end
	//printf("actions on kernel end\n");
	CHECK(gpuvm_kernel_end(a, 0));
	CHECK(gpuvm_kernel_end(b, 0));
	CHECK(gpuvm_kernel_end(c, 0));
}

int main(int argc, char** argv) {
	
	CHECK(gpuvm_pre_init(GPUVM_THREADS_BEFORE_INIT));
	// get device
	cl_device_id dev;
	get_device(&dev);

	// create context
	cl_context ctx = clCreateContext(0, 1, &dev, 0, 0, 0);

	CHECK_NULL(ctx);

	// create queue
	queue = clCreateCommandQueue(ctx, dev, 0, 0);
	CHECK_NULL(queue);

	CHECK(gpuvm_pre_init(GPUVM_THREADS_AFTER_INIT));

	// create program and kernel
	char **sourceLines;
	int lineCount;
	loadSource("src/kernel.cl", &sourceLines, &lineCount);
	cl_program program = clCreateProgramWithSource(ctx, lineCount, (const char**)sourceLines, 0, 0);
	CHECK_NULL(program);
	CHECK(clBuildProgram(program, 1, &dev, 0, 0, 0));
	add_arrays_kernel  = clCreateKernel(program, "add_arrays", 0);
	CHECK_NULL(add_arrays_kernel);

	// initialize GPUVM
	CHECK(gpuvm_init(1, (void**)&queue, 
									 GPUVM_OPENCL | GPUVM_UNLINK_NO_SYNC_BACK | GPUVM_WRITER_SIG_BLOCK));

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
	cl_mem da = clCreateBuffer(ctx, 0, SZ, 0, 0);
	cl_mem db = clCreateBuffer(ctx, 0, SZ, 0, 0);
	cl_mem dc = clCreateBuffer(ctx, 0, SZ, 0, 0);
	CHECK_NULL(da);
	CHECK_NULL(db);
	CHECK_NULL(dc);

	// link host buffers to device buffers
	//printf("linking buffers\n");
	CHECK(gpuvm_link(ha, SZ, 0, (void*)da, GPUVM_OPENCL | GPUVM_ON_HOST));
	CHECK(gpuvm_link(hb, SZ, 0, (void*)db, GPUVM_OPENCL | GPUVM_ON_HOST));
	CHECK(gpuvm_link(hc, SZ, 0, (void*)dc, GPUVM_OPENCL | GPUVM_ON_HOST));

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

	// free OpenCL buffers
	clReleaseMemObject(da);
	clReleaseMemObject(db);
	clReleaseMemObject(dc);

	// free host memory
	free(ha);
	free(hb);
	free(hc);
	free(hg);

	return 0;
}  // end of main()
