/** @file opencl.c implementation of OpenCL calls */

#ifdef __APPLE__
  #include <cl.h>
#else
  #include <CL/cl.h>
#endif

#include <stddef.h>
#include <stdio.h>

#include "gpuvm.h"
#include "opencl.h"
#include "stat.h"
#include "util.h"

/** gets the time between start and end of a finished OpenCL command 
		@param [out] time used to return event time
		@param ev the OpenCL event identifying the command
		@returns 0 if successful and a negative error code if not
*/
static int ocl_time(double *time, cl_event ev) {
	int cl_err;
	cl_ulong start, end;
	// get start time
	cl_err = clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_START, 
																	 sizeof(start), &start,	0);
	if(cl_err != CL_SUCCESS) {
		fprintf(stderr, "ocl_time: can\'t get command start time\n");
		return GPUVM_ERROR;
	}
	// get end time
	cl_err = clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_END, 
																	 sizeof(end), &end, 0);
	if(cl_err != CL_SUCCESS) {
		fprintf(stderr, "ocl_time: can\'t get command end time\n");
		return GPUVM_ERROR;
	}
	// determine return value
	*time = (end - start) * 1e-9;
	return 0;
}  // ocl_time

int ocl_sync_to_host(void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
										 size_t offset) {
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	cl_event ev = 0;
	int cl_err = clEnqueueReadBuffer(queue, devbuf, CL_TRUE, offset, nbytes, hostptr, 0, 0, &ev);
	if(cl_err != CL_SUCCESS) {
		if(cl_err == CL_MEM_OBJECT_ALLOCATION_FAILURE || 
			 cl_err == CL_OUT_OF_RESOURCES || cl_err == CL_OUT_OF_HOST_MEMORY) {
			if(ev) 
				clReleaseEvent(ev);
			return GPUVM_EDEVALLOC;
		} else {
			fprintf(stderr, "ocl_sync_to_host: can\'t copy buffer data");
			if(ev)
				clReleaseEvent(ev);
			return GPUVM_ERROR;
		}
	} else {
		// do statistics collection
		int err = 0;
		if(stat_enabled_g) {
			double time;
			(err = ocl_time(&time, ev)) || (err = stat_acc_double(GPUVM_STAT_COPY_TIME, time));
		}  // if(stat_enabled_g)
		clReleaseEvent(ev);
		return err;
	}
}  // ocl_sync_to_host

int ocl_sync_to_device(const void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
											 size_t offset) {
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	cl_event ev = 0;
	int cl_err = clEnqueueWriteBuffer(queue, devbuf, CL_TRUE, offset, nbytes, hostptr, 0, 0, &ev);
	if(cl_err != CL_SUCCESS) {
		if(cl_err == CL_MEM_OBJECT_ALLOCATION_FAILURE || 
			 cl_err == CL_OUT_OF_RESOURCES || cl_err == CL_OUT_OF_HOST_MEMORY) {
			if(ev)
				clReleaseEvent(ev);
			return GPUVM_EDEVALLOC;
		} else {
			fprintf(stderr, "ocl_sync_to_device: can\'t copy buffer data");
			if(ev)
				clReleaseEvent(ev);
			return GPUVM_ERROR;
		}
	} else {
		// do statistics collection
		int err = 0;
		if(stat_enabled_g) {
			double time;
			(err = ocl_time(&time, ev)) || (err = stat_acc_double(GPUVM_STAT_COPY_TIME, time));
		}  // if(stat_enabled_g)
		clReleaseEvent(ev);
		return err;
	}
}  // ocl_sync_to_device
