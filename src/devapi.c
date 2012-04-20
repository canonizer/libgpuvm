/** @file devapi.c implementation of device interaction API, either CUDA or
		OpenCL 
*/

#include <signal.h>
#include <stdio.h>

#include "cuda-api.h"
#include "devapi.h"
#include "gpuvm.h"
#include "opencl-api.h"
#include "stat.h"
#include "util.h"

devapi_t *devapi_g;

/** a helper signal mask to (un)block during writer lock */
sigset_t devapi_block_sig_g;

int devapi_init(int flags) {
	sigemptyset(&devapi_block_sig_g);
	sigaddset(&devapi_block_sig_g, SIG_MONOGC_SUSPEND);
#ifndef __APPLE__
	sigaddset(&devapi_block_sig_g, SIG_SUSP);
#endif

	flags &= GPUVM_API;
	if(flags != GPUVM_CUDA && flags != GPUVM_OPENCL) {
		fprintf(stderr, "devapi_init: invalid flags\n");
		return GPUVM_EARG;
	}
	if(flags == GPUVM_OPENCL) {
#ifdef OPENCL_ENABLED
		return ocl_devapi_init();
#else
		fprintf(stderr, "devapi_init: OpenCL is not supported "
						"in this libgpuvm build\n");
		return GPUVM_EAPI;
#endif
	}
	if(flags == GPUVM_CUDA) {
#ifdef CUDA_ENABLED
		return cuda_devapi_init();
#else
		fprintf(stderr, "devapi_init: CUDA is not supported "
						"in this libgpuvm build\n");
		return GPUVM_EAPI;		
#endif
	}
}  // devapi_init

int memcpy_h2d
(devapi_t *devapi, unsigned idev, void *tgt, void *src, size_t nbytes, 
 size_t devoff) {
	//sigprocmask(SIG_BLOCK, &devapi_block_sig_g, 0);
	// time API call
	rtime_t start_time, end_time;
	if(stat_enabled()) 
		start_time = rtime_get();

	int err = devapi->memcpy_h2d(idev, tgt, src, nbytes, devoff);
	
	if(stat_enabled()) {
		end_time = rtime_get();
		stat_acc_double(GPUVM_STAT_HOST_COPY_TIME, rtime_diff(&start_time, &end_time));
	}
	//sigprocmask(SIG_UNBLOCK, &devapi_block_sig_g, 0);
	return err;
}  // memcpy_h2d

int memcpy_d2h
(devapi_t *devapi, unsigned idev, void *tgt, void *src, size_t nbytes, 
 size_t devoff) {
	//sigprocmask(SIG_BLOCK, &devapi_block_sig_g, 0);
	// time API call
	rtime_t start_time, end_time;
	if(stat_enabled()) 
		start_time = rtime_get();

	int err = devapi->memcpy_d2h(idev, tgt, src, nbytes, devoff);
	
	if(stat_enabled()) {
		end_time = rtime_get();
		stat_acc_double(GPUVM_STAT_HOST_COPY_TIME, rtime_diff(&start_time, &end_time));
	}
	//sigprocmask(SIG_UNBLOCK, &devapi_block_sig_g, 0);
	return err;
}  // memcpy_d2h
