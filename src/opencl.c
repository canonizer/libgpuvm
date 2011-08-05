/** @file opencl.c implementation of OpenCL calls */

#include <CL/cl.h>
#include <stddef.h>
#include <stdio.h>

#include "gpuvm.h"
#include "opencl.h"
#include "util.h"

int ocl_sync_to_host(void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
										 size_t offset) {
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	//fprintf(stderr, "doing OpenCL device-to-host copying\n");	
	//fprintf(stderr, "queue = %tx\n", queue);
	//fprintf(stderr, "devbuf = %tx\n", devbuf);
	//fprintf(stderr, "offset = %tx\n", offset);
	//fprintf(stderr, "nbytes = %tx\n", nbytes);
	//fprintf(stderr, "hostptr = %tx\n", hostptr);
	int cl_err = clEnqueueReadBuffer(queue, devbuf, CL_TRUE, offset, nbytes, hostptr, 0, 0,
																	 0);
	//clFinish(queue);
	if(cl_err != CL_SUCCESS) {
		if(cl_err == CL_MEM_OBJECT_ALLOCATION_FAILURE || 
			 cl_err == CL_OUT_OF_RESOURCES || cl_err == CL_OUT_OF_HOST_MEMORY) {
			return GPUVM_EDEVALLOC;
		} else {
			fprintf(stderr, "ocl_sync_to_host: can\'t copy buffer data");
			return GPUVM_ERROR;
		}
	} else 
		return 0;
}  // ocl_sync_to_host

int ocl_sync_to_device(const void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
											 size_t offset) {
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	int cl_err = clEnqueueWriteBuffer(queue, devbuf, CL_TRUE, offset, nbytes, hostptr, 0, 0, 0);
	if(cl_err != CL_SUCCESS) {
		if(cl_err == CL_MEM_OBJECT_ALLOCATION_FAILURE || 
			 cl_err == CL_OUT_OF_RESOURCES || cl_err == CL_OUT_OF_HOST_MEMORY) {
			return GPUVM_EDEVALLOC;
		} else {
			fprintf(stderr, "ocl_sync_to_device: can\'t copy buffer data");
			return GPUVM_ERROR;
		}
	} else 
		return 0;
}  // ocl_sync_to_device
