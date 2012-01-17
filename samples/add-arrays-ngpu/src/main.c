/** OpenCL "hello, world!" sample; adding two arrays */

#ifdef __APPLE__
  #include <cl.h>
#else
  #include <CL/cl.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

#define N (1024 * 20 + 128)
#define SZ (N * sizeof(int))
#define NGPUS 2

cl_device_id devs[NGPUS];
cl_context ctxs[NGPUS];
cl_command_queue queues[NGPUS];
cl_program programs[NGPUS];
cl_kernel kernels[NGPUS];
int igpus[NGPUS];
int *ha = 0, *hb = 0, *hc = 0, *hg = 0;

typedef cl_int (*clCreateSubDevices_t)
(cl_device_id in_device, const cl_bitfield *properties, 
 cl_uint num_entries, cl_device_id *out_devices, cl_uint *num_devices);
clCreateSubDevices_t clCreateSubDevices;

void get_devices(cl_device_id devs[NGPUS]) {

	// get platform
	cl_platform_id platform;
	CHECK(clGetPlatformIDs(1, &platform, 0));

	// get device
	int ndevs = 0;
	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, NGPUS, devs, &ndevs);
	if(ndevs == NGPUS)
		return;
	else if(ndevs) {
		devs[1] = devs[0];
		return;
	} else {
		printf("can\'t allocate a GPU device\n");
		exit(-1);
	}
}  // get_device

void *thread_fun(void *ptr) {
	int igpu = *(int*)ptr;
	size_t my_sz = SZ / NGPUS;
	size_t my_n = N / NGPUS;
	size_t offset = my_n * igpu;
	cl_event ev;

	// allocate device data
	cl_mem da = clCreateBuffer(ctxs[igpu], 0, my_sz, 0, 0);
	cl_mem db = clCreateBuffer(ctxs[igpu], 0, my_sz, 0, 0);
	cl_mem dc = clCreateBuffer(ctxs[igpu], 0, my_sz, 0, 0);
	CHECK_NULL(da);
	CHECK_NULL(db);
	CHECK_NULL(dc);

	// link host buffers to device buffers
	//printf("linking buffers\n");
	CHECK(gpuvm_link(ha + offset, my_sz, igpu, (void*)da, GPUVM_OPENCL | GPUVM_ON_HOST));
	CHECK(gpuvm_link(hb + offset, my_sz, igpu, (void*)db, GPUVM_OPENCL | GPUVM_ON_HOST));
	CHECK(gpuvm_link(hc + offset, my_sz, igpu, (void*)dc, GPUVM_OPENCL | GPUVM_ON_HOST));

	// before-kernel actions
	printf("adding arrays\n");
	CHECK(gpuvm_kernel_begin(ha + offset, igpu, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(hb + offset, igpu, GPUVM_READ_WRITE));
	CHECK(gpuvm_kernel_begin(hc + offset, igpu, GPUVM_READ_WRITE));
	
	// run program
	CHECK(clSetKernelArg(kernels[igpu], 0, sizeof(cl_mem), &dc));
	CHECK(clSetKernelArg(kernels[igpu], 1, sizeof(cl_mem), &da));
	CHECK(clSetKernelArg(kernels[igpu], 2, sizeof(cl_mem), &db));
	size_t gws[1] = {my_n};
	size_t lws[1] = {64};
	size_t gwos[1] = {0};
	CHECK(clEnqueueNDRangeKernel(queues[igpu], kernels[igpu], 1, gwos, gws, lws,
	0, 0, &ev));
	printf("thread %d: kernel enqueued\n", igpu);
	//CHECK(clFinish(queues[igpu]));
	CHECK(clWaitForEvents(1, &ev));
	printf("thread %d: kernel finished\n", igpu);

	// on kernel end
	CHECK(gpuvm_kernel_end(ha + offset, igpu));
	CHECK(gpuvm_kernel_end(hb + offset, igpu));
	CHECK(gpuvm_kernel_end(hc + offset, igpu));

	// evaluate "gold" result
	for(int i = offset; i < offset + my_n; i++)
		hg[i] = ha[i] + hb[i];

	// check result
	for(int i = offset; i < offset + my_n; i++) {
		if(hg[i] != hc[i]) {
			printf("check in thread %d: FAILED\n", igpu);
			printf("hg[%d] != hc[%d]: %d != %d\n", i, i, hg[i], hc[i]);
			exit(-1);
		}
	}
	printf("check in thread %d: PASSED\n", igpu);

	return 0;	
}  // thread_fun

int main(int argc, char** argv) {

	// allocate host data
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

	CHECK(gpuvm_pre_init(GPUVM_THREADS_BEFORE_INIT));
	get_devices(devs);

	for(int igpu = 0; igpu < NGPUS; igpu++) {
		// initialize GPU id
		igpus[igpu] = igpu;

		// create context
		ctxs[igpu] = clCreateContext(0, 1, devs + igpu, 0, 0, 0);
		CHECK_NULL(ctxs[igpu]);

		// create queue
		queues[igpu] = clCreateCommandQueue(ctxs[igpu], devs[igpu], 0, 0);
		CHECK_NULL(queues[igpu]);
	}

	CHECK(gpuvm_pre_init(GPUVM_THREADS_AFTER_INIT));

	for(int igpu = 0; igpu < NGPUS; igpu++) {
		// create program and kernel
		char **sourceLines;
		int lineCount;
		loadSource("src/kernel.cl", &sourceLines, &lineCount);
		programs[igpu] = clCreateProgramWithSource
			(ctxs[igpu], lineCount, (const char**)sourceLines, 0, 0);
		CHECK_NULL(programs[igpu]);
		CHECK(clBuildProgram(programs[igpu], 1, devs + igpu, 0, 0, 0));
		kernels[igpu] = clCreateKernel(programs[igpu], "add_arrays", 0);
		CHECK_NULL(kernels[igpu]);
	}  // for(igpu)

	// initialize GPUVM
	CHECK(gpuvm_init(NGPUS, (void**)queues, GPUVM_OPENCL));

	pthread_t threads[NGPUS];
	// fire up threads for separate devices and wait for them to finish
	for(int igpu = 0; igpu < NGPUS; igpu++)
		pthread_create(threads + igpu, 0, thread_fun, igpus + igpu);
	for(int igpu = 0; igpu < NGPUS; igpu++)
		pthread_join(threads[igpu], 0);

	// check result
	for(int i = 0; i < N; i++) {
		if(hg[i] != hc[i]) {
			printf("global check: FAILED\n");
			printf("hg[%d] != hc[%d]: %d != %d\n", i, i, hg[i], hc[i]);
			exit(-1);
		}
	}
	printf("global check: PASSED\n");

	// print result
	printf("printing result\n");
	int step = 1536;
	for(int i = 0; i < N; i ++)
		if(i % step == 0)
			printf("hc[%d] = %d\n", i, hc[i]);

	/*
	// unlink
	CHECK(gpuvm_unlink(ha, 0));
	CHECK(gpuvm_unlink(hb, 0));
	CHECK(gpuvm_unlink(hc, 0));

	// free OpenCL buffers
	clReleaseMemObject(da);
	clReleaseMemObject(db);
	clReleaseMemObject(dc);
	*/
	// free host memory
	free(ha);
	free(hb);
	free(hc);
	free(hg);

	return 0;
}  // end of main()
