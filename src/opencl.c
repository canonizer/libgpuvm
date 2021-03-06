/** @file opencl.c implementation of OpenCL calls */

#ifdef __APPLE__
  #include <cl.h>
#else
  #include <CL/cl.h>
#endif

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "gpuvm.h"
#include "opencl.h"
#include "stat.h"
#include "util.h"

#define MAX_DEVICE_NAME_LENGTH 256

/** event callback to use in AMD queue hack 
		@param event ignored
		@param event_status ignored
		@param user_data ignored
 */
static void event_callback(cl_event event, int event_status, void *user_data) {
	sigset_t signal_mask;
	//sigfillset(&signal_mask);
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGSEGV);
	//sigaddset(&signal_mask, SIGBUS);
	sigprocmask(SIG_UNBLOCK, &signal_mask, 0);
}

int ocl_amd_hack_init(void) {
	unsigned iqueue;
	for(iqueue = 0; iqueue < ndevs_g; iqueue++) {
		cl_command_queue queue = (cl_command_queue)devs_g[iqueue];
		cl_device_id device;
		if(clGetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(cl_device_id),
														 &device, 0) != CL_SUCCESS) {
			fprintf(stderr, "ocl_amd_hack_init: can\'t get device id\n");
			return -1;
		}	 
		cl_device_type device_type;
		if(clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(cl_device_type), 
											 &device_type, 0) != CL_SUCCESS) {
			fprintf(stderr, "ocl_amd_hack_init: can\'t get device type\n");
			return -1;
		}
		if(device_type != CL_DEVICE_TYPE_GPU)
			continue;
		cl_platform_id platform;
		if(clGetDeviceInfo(device, CL_DEVICE_PLATFORM, sizeof(cl_platform_id),
											 &platform, 0) != CL_SUCCESS) {
			fprintf(stderr, "ocl_amd_hack_init: can\'t get device platform\n");
			return -1;
		}		
		char platform_name[MAX_DEVICE_NAME_LENGTH + 1];
		memset(platform_name, 0, sizeof(platform_name));
		if(clGetPlatformInfo(platform, CL_PLATFORM_NAME, MAX_DEVICE_NAME_LENGTH + 1, 
											platform_name, 0) != CL_SUCCESS) {
			fprintf(stderr, "ocl_amd_hack_init: can\'t get platform name\n");
			return -1;
		}
		if(strstr(platform_name, "AMD") != platform_name) 
			continue;

		// it's AMD GPU and platform; do the hack; do not check for errors
		cl_event ev;
		clEnqueueMarker(queue, &ev);
		clSetEventCallback(ev, CL_COMPLETE, event_callback, 0);
		clFlush(queue);
		clReleaseEvent(ev);
	}  // for()
}  // 

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
	rtime_t start_time, end_time;
	if(stat_enabled()) 
		start_time = rtime_get();
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	cl_event ev = 0;
	//fprintf(stderr, "copying data back\n");
	int cl_err = clEnqueueReadBuffer(queue, devbuf, CL_FALSE, offset, nbytes,
										 hostptr, 0, 0, &ev);
	clWaitForEvents(1, &ev);
	//fprintf(stderr, "copied data back\n");
	//clFinish(queue);
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
		if(stat_enabled()) {
			double time;
			(err = ocl_time(&time, ev)) || (err = stat_acc_double(GPUVM_STAT_COPY_TIME, time));
		}  // if(stat_enabled_g)
		if(stat_enabled()) {
			end_time = rtime_get();
			double time = rtime_diff(&start_time, &end_time);
			stat_acc_double(GPUVM_STAT_HOST_COPY_TIME, time);
			stat_acc_unblocked_double(GPUVM_STAT_PAGEFAULT_TIME, -time);
		}
		clReleaseEvent(ev);
		return err;
	}
}  // ocl_sync_to_host

int ocl_sync_to_device(const void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
											 size_t offset) {
	rtime_t start_time, end_time;
	if(stat_enabled()) 
		start_time = rtime_get();
	cl_command_queue queue = (cl_command_queue)devs_g[idev];
	cl_mem buffer = (cl_mem)devbuf;
	cl_event ev = 0;
	//fprintf(stderr, "copying data to device\n");
	int cl_err = clEnqueueWriteBuffer(queue, devbuf, CL_FALSE, offset, nbytes,
																		hostptr, 0, 0, &ev);
	//fprintf(stderr, "copied data to device\n");
	clWaitForEvents(1, &ev);
	//clFinish(queue);
	if(cl_err != CL_SUCCESS) {
		if(cl_err == CL_MEM_OBJECT_ALLOCATION_FAILURE || 
			 cl_err == CL_OUT_OF_RESOURCES || cl_err == CL_OUT_OF_HOST_MEMORY) {
			if(ev)
				clReleaseEvent(ev);
			return GPUVM_EDEVALLOC;
		} else {
			fprintf(stderr, "ocl_sync_to_device: can\'t copy buffer data\n");
			if(ev)
				clReleaseEvent(ev);
			return GPUVM_ERROR;
		}
	} else {
		// do statistics collection
		int err = 0;
		if(stat_enabled()) {
			double time;
			(err = ocl_time(&time, ev)) || (err = stat_acc_double(GPUVM_STAT_COPY_TIME, time));
		}  // if(stat_enabled_g)
		clReleaseEvent(ev);
		if(stat_enabled()) {
			end_time = rtime_get();
			stat_acc_double(GPUVM_STAT_HOST_COPY_TIME, rtime_diff(&start_time, &end_time));
		}
		return err;
	}
}  // ocl_sync_to_device
