/** @file gpuvm.c implementation of public GPUVM interface, except for stat collection */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devapi.h"
#include "gpuvm.h"
#include "handler.h"
#include "host-array.h"
#include "link.h"
#include "stat.h"
#include "subreg.h"
#include "tsem.h"
#include "util.h"

unsigned ndevs_g = 0;
void **devs_g = 0;

/** threads allocated before runtime */
static thread_t *pre_runtime_threads_g = 0;
static unsigned pre_runtime_nthreads_g = 0;

int gpuvm_library_exists() {
	return 1;
}  // gpuvm_library_exists

int gpuvm_pre_init(int flags) {
	if(flags != GPUVM_THREADS_BEFORE_INIT && 
		 flags != GPUVM_THREADS_AFTER_INIT) {
		fprintf(stderr, "gpuvm_pre_init: invalid flags\n");
		return GPUVM_EARG;
	}
	if(flags == GPUVM_THREADS_BEFORE_INIT) {
		// case before OpenCL initialization
		if(pre_runtime_threads_g)	{		
			free(pre_runtime_threads_g);
			pre_runtime_threads_g = 0;
			pre_runtime_nthreads_g = 0;
		}
		int pre_runtime_nthreads = get_threads(&pre_runtime_threads_g);
		if(pre_runtime_nthreads < 0)
			return -1;
		pre_runtime_nthreads_g = pre_runtime_nthreads;
		//fprintf(stderr, "%d threads before OpenCL runtime init\n", 
		//				pre_runtime_nthreads_g);
		return 0;
	} else if(flags == GPUVM_THREADS_AFTER_INIT) {
		// case after OpenCL initialization
		if(!pre_runtime_threads_g) {
			fprintf(stderr, "gpuvm_pre_init: list of threads must be recorded "
							"first\n");
			return GPUVM_ESTATE;			
		}
		thread_t *after_runtime_threads, *diff_threads;
		// get new threads
		int after_runtime_nthreads = get_threads(&after_runtime_threads);
		if(after_runtime_nthreads < 0)
			return after_runtime_nthreads;
		//fprintf(stderr, "%d threads after OpenCL runtime init\n", 
		//				after_runtime_nthreads);
		// get difference
		int diff_nthreads = threads_diff
			(&diff_threads, after_runtime_threads, after_runtime_nthreads,
			 pre_runtime_threads_g, pre_runtime_nthreads_g);
		if(diff_nthreads < 0) {
			free(after_runtime_threads);
			return diff_nthreads;
		}
		// save difference as "immune threads"
		if(diff_nthreads > MAX_NTHREADS) {
			fprintf(stderr, "gpuvm_pre_init: too many immune threads\n");
			return -1;
		}
		immune_nthreads_g = diff_nthreads;
		//fprintf(stderr, "immune_nthreads_g = %d\n", immune_nthreads_g);
		memcpy(immune_threads_g, diff_threads, immune_nthreads_g * sizeof(thread_t));
		free(after_runtime_threads);
		free(diff_threads);
		return 0;
	}  // if(flags == ...)
}

int gpuvm_init(unsigned ndevs, void **devs, int flags) {
	// check arguments
	if(ndevs == 0) {
		fprintf(stderr, "gpuvm_init: zero devices not allowed\n");
		return GPUVM_EARG;
	}
	if(flags & ~(GPUVM_API | GPUVM_STAT | GPUVM_WRITER_SIG_BLOCK | 
							 GPUVM_UNLINK_NO_SYNC_BACK) || !(flags & GPUVM_API)) {
		fprintf(stderr, "gpuvm_init: invalid flags\n");
		return GPUVM_EARG;
	}

	// check state
	if(ndevs_g) {
		fprintf(stderr, "gpuvm_init: GPUVM already initialized\n");
		return GPUVM_ETWICE;
	}
	ndevs_g = ndevs;

	// initialize auxiliary structures
	int err = 0;
	err = salloc_init();
	if(err)
		return err;

	// initialize devices
	devs_g = (void**)smalloc(ndevs * sizeof(void*));
	if(!devs_g)
		return GPUVM_ESALLOC;

	if(flags & GPUVM_OPENCL) {
		if(!devs) {
			fprintf(stderr, "gpuvm_init: null pointer to devices not allowed\n");
			return GPUVM_ENULL;
		}
		memcpy(devs_g, devs, ndevs * sizeof(void*));
	} else if(flags & GPUVM_CUDA) {
		// ignore devs, just zero out devs_g
		memset(devs_g, 0, ndevs * sizeof(void*));
	}

	// continue with initialization
	(err = sync_init()) || 
		(err = devapi_init(flags)) ||
		(err = handler_init()) || 
		(err = stat_init(flags)) || 
		(err = tsem_init()) || 
		(err = wthreads_init());
	if(err)
		return err;
	
	return 0;
}  // gpuvm_init

int gpuvm_link(void *hostptr, size_t nbytes, unsigned idev, void *devbuf, int flags) {
	// check arguments
	if(!hostptr) {
		fprintf(stderr, "gpuvm_link: hostptr is NULL\n");
		return GPUVM_ENULL;
	}
	if(nbytes == 0) {
		fprintf(stderr, "gpuvm_link: nbytes is zero\n");
		return GPUVM_EARG;
	}
	if(idev >= ndevs_g) {
		fprintf(stderr, "gpuvm_link: invalid device number\n");
		return GPUVM_EARG;
	}
	if((flags & ~GPUVM_API) != GPUVM_ON_HOST && 
		 (flags & ~GPUVM_API) != GPUVM_ON_DEVICE) {
		fprintf(stderr, "gpuvm_link: invalid flags\n");
		return GPUVM_EARG;
	}
	if(!devbuf) {
		fprintf(stderr, "gpuvm_link: device buffer cannot be null\n");
		return GPUVM_ENULL;
	}

	// lock writer data structure
	if(lock_writer())
		return GPUVM_ERROR;

	// find an array intersecting specified range
	host_array_t *host_array = 0;
	if(host_array_find(&host_array, hostptr, nbytes)) {
		//fprintf(stderr, "gpuvm_link: intersecting range already registered with GPUVM\n");
		//fprintf(stderr, "ptr=%p, nbytes=%zd, rangeptr=%p, rangenbytes=%zd\n", 
		//				hostptr, nbytes, host_array->range.ptr, host_array->range.nbytes);
		unlock_writer();
		return GPUVM_ERANGE;
	}
	//fprintf(stderr, "host array search finished\n");
	if(host_array) {
		if(host_array->links[idev]) {
			//fprintf(stderr, "gpuvm_link: link on specified device already exists\n");
			unlock_writer();
			return GPUVM_ELINK;
		}
		if(flags & GPUVM_ON_DEVICE) {
			fprintf(stderr, "gpuvm_link: on-device linking of a registered " 
							"array is not allowed\n");
			unlock_writer();
			return GPUVM_ETWICE;
		}
	}

	// allocate an array if not found
	host_array_t *new_host_array = 0;
	int err = 0;
	if(!host_array) {
		err = host_array_alloc(&new_host_array, hostptr, nbytes, 
													 flags & GPUVM_ON_DEVICE ? idev : -1);
		//fprintf(stderr, "new host array allocated\n");
		if(err) { 
			unlock_writer();
			return err;
		}
		host_array = new_host_array;
	} else {
		//fprintf(stderr, "using old host array\n");
	}

	// create a link with an array (and assign it into the array)
	link_t *link = 0;
	err = link_alloc(&link, devbuf, idev, host_array);
	//fprintf(stderr, "new link allocated\n");
	if(err) {
		host_array_free(new_host_array);
		unlock_writer();
		return err;
	}

	if(unlock_writer())
		return GPUVM_ERROR;
	//fprintf(stderr, "gpuvm linked\n");
	return 0;
}  // gpuvm_link

/** pre-unlinks the host array by synchronizing it back to host and unprotecting
		it 
		@param hostptr the address of the host array to be synchronized and
		unprotected
		@returns 0 if successful and a negative error code if not
*/
static int gpuvm_pre_unlink(void *hostptr) {
	if(lock_reader())
		return GPUVM_ERROR;

	// get the array
	host_array_t *host_array = 0;
	host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		unlock_reader();
		fprintf(stderr, "gpuvm_pre_unlink: not a valid pointer\n");
		return GPUVM_EHOSTPTR;
	}
	
	// tap into the beginning of each array subregion, to cause readback if
	// mprotected 
	unsigned isubreg;
	int err;
	for(isubreg = 0; isubreg < host_array->nsubregs; isubreg++) {
		err += *(char*)host_array->subregs[isubreg]->range.ptr;
	}
	
	unlock_reader();
	return 0;
}  // gpuvm_pre_unlink()

int gpuvm_unlink(void *hostptr, unsigned idev) {
	// check arguments
	if(idev >= ndevs_g) {
		fprintf(stderr, "gpuvm_unlink: invalid device number\n");
		return GPUVM_EARG;
	}
	if(!hostptr)
		return 0;

	if(stat_unlink_sync_back()) {
		// make array to be synced to host and unprotected
		gpuvm_pre_unlink(hostptr);
	}

	if(lock_writer())
		return GPUVM_ERROR;

	host_array_t *host_array;
	int err = host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		unlock_writer();
		fprintf(stderr, "gpuvm_unlink: not a valid pointer\n");
		return GPUVM_EHOSTPTR;
	}
	if(err = host_array_remove_link(host_array, idev)) {
		unlock_writer();
		return err;
	}
	if(!host_array_has_links(host_array))
		host_array_free(host_array);

	if(unlock_writer())
		return GPUVM_ERROR;

	return 0;
}  // gpuvm_unlink

void *gpuvm_xlate(void *hostptr, unsigned idev) {
	// check arguments
	if(!hostptr || idev >= ndevs_g)
		return 0;
	
	// lock for reading
	if(lock_reader())
		return 0;

	// find host array and device buffer
	host_array_t *host_array;
	void *dev_buffer = 0;
	int err = host_array_find(&host_array, hostptr, 0);
	if(host_array && host_array->links[idev])
		dev_buffer = host_array->links[idev]->buf;
	
	// unlock and return
	if(unlock_reader())
		return 0;
	return dev_buffer;
}  // gpuvm_xlate

int gpuvm_kernel_begin(void *hostptr, unsigned idev, int flags) {
	// check arguments
	if(!hostptr) {
		fprintf(stderr, "gpuvm_kernel_begin: hostptr is NULL\n");
		return GPUVM_ENULL;
	}
	if(idev >= ndevs_g) {
		fprintf(stderr, "gpuvm_kernel_begin: invalid device number\n");
		return GPUVM_EARG;		
	}
	if(flags != GPUVM_READ_WRITE) {
		fprintf(stderr, "gpuvm_kernel_begin: invalid flags\n");
		return GPUVM_EARG;
	}

	if(lock_reader())
		return GPUVM_ERROR;

	// find host array
	host_array_t *host_array;
	host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		fprintf(stderr, "gpuvm_kernel_begin: hostptr is not registed with GPUVM\n");
		unlock_reader();
		return GPUVM_EHOSTPTR;
	}
	
	// copy data to device if needed
	int err;
	if(err = host_array_sync_to_device(host_array, idev)) {
		unlock_reader();
		return err;
	}
	
	if(unlock_reader())
		return GPUVM_ERROR;

	return 0;
}  // gpuvm_kernel_begin

int gpuvm_kernel_end(void *hostptr, unsigned idev) {
	// check arguments
	if(!hostptr) {
		fprintf(stderr, "gpuvm_kernel_end: hostptr is NULL");
		return GPUVM_ENULL;
	}
	if(idev >= ndevs_g) {
		fprintf(stderr, "gpuvm_kernel_end: invalid device number");
		return GPUVM_EARG;
	}
	
	// lock for writer
	if(lock_writer())
		return GPUVM_ERROR;

	// find host array
	host_array_t *host_array;
	host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		fprintf(stderr, "gpuvm_kernel_begin: hostptr is not registed with GPUVM\n");
		unlock_writer();
		return GPUVM_EHOSTPTR;
	}

	// set up memory protection and update actuality info
	int err;
	if(err = host_array_after_kernel(host_array, idev)) {
		unlock_writer();
		return err;
	}

	// lock for writer
	if(unlock_writer())
		return GPUVM_ERROR;

	return 0;
} // gpuvm_kernel_end
