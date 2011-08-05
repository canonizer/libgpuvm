/** OpenCL "hello, world!" sample; adding two arrays */

#include <CL/cl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#include <gpuvm.h>

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

#define N (1024 * 64)
#define SZ (N * sizeof(int))


int main(int argc, char** argv) {

	// get platform
	cl_platform_id platform;
	CHECK(clGetPlatformIDs(1, &platform, 0));

	// get device
	cl_device_id dev;
	CHECK(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &dev, 0));

	// create context
	cl_context ctx = clCreateContext(0, 1, &dev, 0, 0, 0);
	CHECK_NULL(ctx);

	// create queue
	cl_command_queue queue = clCreateCommandQueue(ctx, dev, 0, 0);
	CHECK_NULL(queue);

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
	/*int *ha = (int*)malloc(SZ);
	int *hb = (int*)malloc(SZ);
	int *hc = (int*)malloc(SZ);*/
	int *ha = (int*)memalign(GPUVM_PAGE_SIZE, SZ);
	int *hb = (int*)memalign(GPUVM_PAGE_SIZE, SZ);
	int *hc = (int*)memalign(GPUVM_PAGE_SIZE, SZ);
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
	//printf("actions on kernel begin\n");
	CHECK(gpuvm_kernel_begin(ha, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(hb, 0, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(hc, 0, GPUVM_READ_WRITE));
	
	// run program
	CHECK(clSetKernelArg(kernel, 0, sizeof(cl_mem), &dc));
	CHECK(clSetKernelArg(kernel, 1, sizeof(cl_mem), &da));
	CHECK(clSetKernelArg(kernel, 2, sizeof(cl_mem), &db));
	size_t gws[1] = {N};
	size_t lws[1] = {256};
	size_t gwos[1] = {0};
	CHECK(clEnqueueNDRangeKernel(queue, kernel, 1, gwos, gws, lws, 0, 0, 0));
	CHECK(clFinish(queue));

	// on kernel end
	//printf("actions on kernel end\n");
	CHECK(gpuvm_kernel_end(ha, 0));
	CHECK(gpuvm_kernel_end(hb, 0));
	CHECK(gpuvm_kernel_end(hc, 0));

	// print result
	printf("printing result\n");
	int step = 128;
	for(int i = 0; i < N; i += step)
		printf("hc[%d] = %d\n", i, hc[i]);

	return 0;
}  // end of main()