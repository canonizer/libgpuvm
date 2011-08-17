#include <stdio.h>
#include <string.h>

#include "handler.h"
#include "host-array.h"
#include "link.h"
#include "gpuvm.h"
#include "util.h"


unsigned ndevs_g = 0;
void **devs_g = 0;


int gpuvm_init(unsigned ndevs, void **devs, int flags) {
	int err;
	// check arguments
	if(ndevs == 0) {
		fprintf(stderr, "zero devices not allowed");
		return GPUVM_EARG;
	}
	if(!devs) {
		fprintf(stderr, "null pointer to devices not allowed");
		return GPUVM_ENULL;
	}
	if(flags != GPUVM_OPENCL) {
		fprintf(stderr, "invalid flags");
		return GPUVM_EARG;
	}

	// check state
	if(ndevs_g) {
		fprintf(stderr, "GPUVM already initialized");
		return GPUVM_ETWICE;
	}
	ndevs_g = ndevs;

	// initialize auxiliary structures
	if(err = sync_init())
		return err;
	if(err = salloc_init())
		return err;
	if(err = handler_init()) 
		return err;

	// initialize devices
	devs_g = (void**)smalloc(ndevs * sizeof(void*));
	if(!devs_g)
		return GPUVM_ESALLOC;
	memcpy(devs_g, devs, ndevs * sizeof(void*));

	// TODO: initialize other structures
	
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
	if(flags != (GPUVM_OPENCL | GPUVM_ON_HOST)) {
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
		fprintf(stderr, "gpuvm_link: intersecting range already registered with GPUVM\n");
		fprintf(stderr, "ptr=%tx, nbytes=%td, rangeptr=%tx, rangenbytes=%td\n", 
						hostptr, nbytes, host_array->range.ptr, host_array->range.nbytes);
		sync_unlock();
		return GPUVM_ERANGE;
	}
	if(host_array && host_array->links[idev]) {
		fprintf(stderr, "gpuvm_link: link on specified device already exists\n");
		sync_unlock();
		return GPUVM_ELINK;
	}
	//fprintf(stderr, "host array not found, allocating new one\n");

	// allocate an array if not found
	host_array_t *new_host_array = 0;
	int err = 0;
	if(!host_array) {
		err = host_array_alloc(&new_host_array, hostptr, nbytes, flags);
		if(err) { 
			sync_unlock();
			return err;
		}
		host_array = new_host_array;
	}
	//fprintf(stderr, "allocating a new link");
	// create a link with an array (and assign it into the array)
	link_t *link = 0;
	err = link_alloc(&link, devbuf, idev, host_array);
	if(err) {
		host_array_free(new_host_array);
		sync_unlock();
		return err;
	}

	if(sync_unlock())
		return GPUVM_ERROR;
	return 0;
}  // gpuvm_link

int gpuvm_unlink(void *hostptr, unsigned idev) {
	// check arguments
	if(idev >= ndevs_g) {
		fprintf(stderr, "gpuvm_unlink: invalid device number\n");
		return GPUVM_EARG;
	}
	if(!hostptr)
		return 0;

	if(lock_writer())
		return GPUVM_ERROR;

	host_array_t *host_array;
	int err = host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		fprintf(stderr, "gpuvm_unlink: not a valid pointer");
		sync_unlock();
		return GPUVM_EHOSTPTR;
	}
	if(err = host_array_remove_link(host_array, idev)) {
		sync_unlock();
		return err;
	}
	if(!host_array_has_links(host_array))
		host_array_free(host_array);

	if(sync_unlock())
		return GPUVM_ERROR;

	return 0;
}  // gpuvm_unlink

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
		sync_unlock();
		return GPUVM_EHOSTPTR;
	}
	
	// copy data to device if needed
	int err;
	if(err = host_array_sync_to_device(host_array, idev)) {
		sync_unlock();
		return err;
	}
	
	if(sync_unlock())
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
	
	// lock for reader
	if(lock_reader())
		return GPUVM_ERROR;

	// find host array
	host_array_t *host_array;
	host_array_find(&host_array, hostptr, 0);
	if(!host_array) {
		fprintf(stderr, "gpuvm_kernel_begin: hostptr is not registed with GPUVM\n");
		sync_unlock();
		return GPUVM_EHOSTPTR;
	}

	// set up memory protection and update actuality info
	int err;
	if(err = host_array_after_kernel(host_array, idev)) {
		sync_unlock();
		return err;
	}

	// lock for writer
	if(sync_unlock())
		return GPUVM_ERROR;

	return 0;
} // gpuvm__kernel_end
