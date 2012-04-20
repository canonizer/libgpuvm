/** @file subreg.c implementation of subreg_t */

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "devapi.h"
#include "gpuvm.h"
#include "host-array.h"
#include "link.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

int subreg_alloc(subreg_t **p, void *hostptr, size_t nbytes, int idev) {
	// allocate memory
	*p = 0;
	subreg_t *new_subreg = (subreg_t*)smalloc(sizeof(subreg_t));
	//fprintf(stderr, "memory for subregion allocated\n");
	if(!new_subreg)
		return GPUVM_ESALLOC;
	memset(new_subreg, 0, sizeof(subreg_t));
	new_subreg->range.ptr = hostptr;
	new_subreg->range.nbytes = nbytes;
	// initialize members
	if(idev >= 0) {
		new_subreg->actual_device = idev;
		new_subreg->actual_host = 0;
		new_subreg->actual_mask = 1ul << idev;
	} else {
		new_subreg->actual_device = NO_ACTUAL_DEVICE;
		new_subreg->actual_host = 1;
		new_subreg->actual_mask = 0;
	}
	if(pthread_mutex_init(&new_subreg->mutex, 0)) {
		fprintf(stderr, "subreg_alloc: can\'t init mutex");
	 	sfree(new_subreg);
		return GPUVM_ERROR;
	}

	// allocate or find region for this subregion
	int err;
	region_t *region = region_find_region(hostptr);
	//fprintf(stderr, "region search finished, region=%x\n", region);
	if(region) {
		// add to existing region
		err = region_add_subreg(region, new_subreg);
		//fprintf(stderr, "added to existing region\n");
	} else {
		// create new region
		err = region_alloc(0, new_subreg);
		//fprintf(stderr, "region allocated\n");
	}
	if(err) {
		//pthread_mutex_destroy(&new_subreg->mutex);
		sfree(new_subreg);
		return err;
	}
	// protect region if the subregion is initially on device
	region = new_subreg->region;
	if(idev >= 0) {
		err = region_protect_after(region, GPUVM_READ_WRITE);
		if(err) {
			subreg_free(new_subreg);
			return err;
		}
	}  // if(on device)

	// return
	*p = new_subreg;
	return 0;
}  // subreg_alloc()

void subreg_free(subreg_t *subreg) {
	// detach subregion from region
	//fprintf(stderr, "removing from region\n");
	region_t *region = subreg->region;
	region_remove_subreg(subreg->region, subreg);

	// free region if empty
	if(!region->nsubregs) {
		//fprintf(stderr, "removing region\n");
		region_free(region);
	}

	pthread_mutex_destroy(&subreg->mutex);
	sfree(subreg);
	//fprintf(stderr, "subreg freed\n");
}

static int subreg_lock(subreg_t *subreg) {
	if(pthread_mutex_lock(&subreg->mutex)) {
		fprintf(stderr, "subreg_lock: can\'t lock mutex");
		return GPUVM_ERROR;
	}
	return 0;
}

static int subreg_unlock(subreg_t *subreg) {
	if(pthread_mutex_unlock(&subreg->mutex)) {
		fprintf(stderr, "subreg_unlock: can\'t unlock mutex");
		return GPUVM_ERROR;
	}
	return 0;
}

/** a simple wrapper for copying data to device 
		@param subreg specifies host subregion to copy
		@param link specifies device buffer to copy
 */
static int subreg_link_sync_to_device
(const subreg_t *subreg, const link_t *link) {
	return memcpy_h2d
		(devapi_g, link->idev, link->buf, subreg->range.ptr, subreg->range.nbytes, 
		 subreg->range.ptr - subreg->host_array->range.ptr);
}

/** a simple wrapper for copying data to host 
		@param subreg specifies host subregion to copy
		@param link specifies device buffer to copy
 */
static int subreg_link_sync_to_host
(const subreg_t *subreg, const link_t* link) {
	return memcpy_d2h
		(devapi_g, link->idev, subreg->range.ptr, link->buf, subreg->range.nbytes,
		 subreg->range.ptr - subreg->host_array->range.ptr);
}

int subreg_sync_to_device(subreg_t *subreg, unsigned idev, int flags) {
	flags &= GPUVM_READ_WRITE;
	int err;

	// check usage info
	// TODO: optionally, detect invalid sharing
	if(err = subreg_lock(subreg))
		return err;
	subreg->device_usage_count++;
	if(subreg->device_usage != flags && 
		 !(flags == GPUVM_READ_ONLY && subreg->device_usage == GPUVM_READ_WRITE))
		subreg->device_usage = flags;
	if(err = subreg_unlock(subreg))
		return err;

	if(!((subreg->actual_mask >> idev) & 1ul)) {
		// need to copy to device
		// lock region and remove protection if it is in place
		region_t *region = subreg->region;
		host_array_t* host_array = subreg->host_array;

		// "remove" protection by causing segmentation fault if region is protected
		err += *(char*)subreg->range.ptr;
		
		// need to copy from host to this device
		link_t *link = host_array->links[idev];
		//fprintf(stderr, "host -> device, subreg = %p, link = %p\n", subreg, link);
		if(err = subreg_link_sync_to_device(subreg, link)) {
			return err;
		}
		// TODO: check these things for atomicity
		subreg->actual_device = idev;
		subreg->actual_mask |= 1ul << idev;

	}  // if already on device
	return 0;
}  // subreg_sync_to_device

int subreg_sync_to_host(subreg_t *subreg) {
	int err;

	// check if already on host
	if(!subreg->actual_host) {
		// have to copy from actual device
		unsigned idev = subreg->actual_device;
		host_array_t *host_array = subreg->host_array;

		// do actualy copying
		link_t *link = host_array->links[idev];
		//fprintf(stderr, "device -> host, subreg = %p, link = %p\n", subreg, link);
		if(err = subreg_link_sync_to_host(subreg, link)) {
			return err;
		}		
	}  // if(!actual_on_host)	
	// device ALWAYS uses actuality when subregion is synced to host
	subreg->actual_host = 1;
	subreg->actual_device = NO_ACTUAL_DEVICE;
	subreg->actual_mask = 0ul;

	return 0;
}  // subreg_sync_to_host

int subreg_after_kernel(subreg_t *subreg, unsigned idev) {

	int err;

	// update subregion actuality
	if(subreg->device_usage == GPUVM_READ_WRITE) {
		subreg->actual_host = 0;
		subreg->actual_device = idev;
		subreg->actual_mask = 1ul << idev;
	} else if(subreg->device_usage == GPUVM_READ_ONLY) {
		// do nothing here
	} else {
		// this is an error
		fprintf(stderr, "subreg_after_kernel: invalid usage flags\n");
		return -1;
	}

	region_t *region = subreg->region;

	// turn on region memory protection
	if(err = region_protect_after(region, subreg->device_usage))
		return err;

	// update usage info; locking is unnecessary due to global lock
	subreg->device_usage_count--;
	if(!subreg->device_usage_count) 
		subreg->device_usage = 0;

	return 0;
}  // subreg_after_kernel
