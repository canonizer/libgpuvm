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

int main(int argc, char** argv) {
	
	CHECK(gpuvm_pre_init(GPUVM_THREADS_BEFORE_INIT));
	// get device
	cl_device_id dev;
	get_device(&dev);

	// create context
	cl_context ctx = clCreateContext(0, 1, &dev, 0, 0, 0);

	CHECK_NULL(ctx);

	// create queue
	cl_command_queue queue = clCreateCommandQueue(ctx, dev, 0, 0);
	CHECK_NULL(queue);

	CHECK(gpuvm_pre_init(GPUVM_THREADS_AFTER_INIT));

	// create program and kernel
	char **sourceLines;
	int lineCount;
	loadSource("src/kernel.cl", &sourceLines, &lineCount);
	cl_program program = clCreateProgramWithSource(ctx, lineCount, (const char**)sourceLines, 0, 0);
	CHECK_NULL(program);
	CHECK(clBuildProgram(program, 1, &dev, 0, 0, 0));
	cl_kernel kernel = clCreateKernel(program, "add_arrays", 0);
	CHECK_NULL(kernel);

	// initialize GPUVM
	CHECK(gpuvm_init(1, (void**)&queue, GPUVM_OPENCL));

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
		CHECK(gpuvm_kernel_begin(ha, 0, GPUVM_READ_WRITE));
		CHECK(gpuvm_kernel_begin(hb, 0, GPUVM_READ_WRITE));
		CHECK(gpuvm_kernel_begin(hc, 0, GPUVM_READ_WRITE));
	
		// run program
		CHECK(clSetKernelArg(kernel, 0, sizeof(cl_mem), &dc));
		CHECK(clSetKernelArg(kernel, 1, sizeof(cl_mem), &da));
		CHECK(clSetKernelArg(kernel, 2, sizeof(cl_mem), &db));
		size_t gws[1] = {N};
		size_t lws[1] = {64};
		size_t gwos[1] = {0};
		CHECK(clEnqueueNDRangeKernel(queue, kernel, 1, gwos, gws, lws, 0, 0, 0));
		CHECK(clFinish(queue));

		// on kernel end
		//printf("actions on kernel end\n");
		CHECK(gpuvm_kernel_end(ha, 0));
		CHECK(gpuvm_kernel_end(hb, 0));
		CHECK(gpuvm_kernel_end(hc, 0));

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
