/** @file subreg.c implementation of subreg_t */

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "gpuvm.h"
#include "host-array.h"
#include "link.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

int subreg_alloc(subreg_t **p, void *hostptr, size_t nbytes) {
	// allocate memory
	*p = 0;
	subreg_t *new_subreg = (subreg_t*)smalloc(sizeof(subreg_t));
	if(!new_subreg)
		return GPUVM_ESALLOC;
	memset(new_subreg, 0, sizeof(subreg_t));
	
	// initialize members
	new_subreg->range.ptr = hostptr;
	new_subreg->range.nbytes = nbytes;
	new_subreg->actual_device = NO_ACTUAL_DEVICE;
	new_subreg->actual_host = 1;
	new_subreg->actual_mask = 0;
	// if(pthread_mutex_init(&new_subreg->mutex, 0)) {
	//	fprintf(stderr, "subreg_alloc: can\'t init mutex");
	//	sfree(new_subreg);
	//	return GPUVM_ERROR;
	//}
	//fprintf(stderr, "subreg: ptr=%tx, nbytes=%td\n", hostptr, nbytes);

	//fprintf(stderr, "searching for a region\n");
	// allocate or find region for this subregion
	int err;
	region_t *region = region_find_region(hostptr);
	if(region) {
		//fprintf(stderr, "adding to an existing region\n");
		// add to existing region
		err = region_add_subreg(region, new_subreg);
	} else {
		// create new region
		//fprintf(stderr, "allocating a new region\n");
		err = region_alloc(0, new_subreg);
	}
	if(err) {
		//pthread_mutex_destroy(&new_subreg->mutex);
		sfree(new_subreg);
		return err;
	}

	//fprintf(stderr, "subregion allocated\n");
	// return
	*p = new_subreg;
	return 0;
}  // subreg_alloc()

void subreg_free(subreg_t *subreg) {
	region_remove_subreg(subreg->region, subreg);
	// pthread_mutex_destroy(&subreg->mutex);
	sfree(subreg);
}

static int subreg_lock(subreg_t *subreg) {
	// no-op - due to global lock
	// if(pthread_mutex_lock(&subreg->mutex)) {
	//	fprintf(stderr, "subreg_lock: can\'t lock mutex");
	//	return GPUVM_ERROR;
	// }
	return 0;
}

static int subreg_unlock(subreg_t *subreg) {
	// no-op - due to global lock
	//if(pthread_mutex_unlock(&subreg->mutex)) {
	//	fprintf(stderr, "subreg_unlock: can\'t unlock mutex");
	//	return GPUVM_ERROR;
	//}
	return 0;
}

/** a simple wrapper for copying data to device 
		@param subreg specifies host subregion to copy
		@param link specifies device buffer to copy
 */
static int subreg_link_sync_to_device(const subreg_t *subreg, const link_t* link) {
	return ocl_sync_to_device
		(subreg->range.ptr, subreg->range.nbytes, link->idev, link->buf, 
		 subreg->range.ptr - subreg->host_array->range.ptr);
}

/** a simple wrapper for copying data to host 
		@param subreg specifies host subregion to copy
		@param link specifies device buffer to copy
 */
static int subreg_link_sync_to_host(const subreg_t *subreg, const link_t* link) {
	//fprintf(stderr, "syncing data to host\n");
	return ocl_sync_to_host
		(subreg->range.ptr, subreg->range.nbytes, link->idev, link->buf, 
		 subreg->range.ptr - subreg->host_array->range.ptr);
}

int subreg_sync_to_device(subreg_t *subreg, unsigned idev) {
	int err;
	if(err = subreg_lock(subreg))
		return err;
	if(!((subreg->actual_mask >> idev) & 1ul)) {
		// need to copy to device
		// lock region and remove protection if it is in place
		region_t *region = subreg->region;
		if(err = region_lock(region)) {
			subreg_unlock(subreg);
			return err;
		}
		// TODO: rewrite it to handle multiple threads
		
		int region_protected = region_is_protected(region);
		if(region_protected) {
			if(err = region_unprotect(region)) {
				region_unlock(region);
				subreg_unlock(subreg);
				return err;
			}
		}

		host_array_t *host_array = subreg->host_array;
		if(!subreg->actual_host && subreg->actual_device != NO_ACTUAL_DEVICE) {
			// need to copy to host from other device first
			if(err = subreg_link_sync_to_host(subreg, host_array->links[subreg->actual_device])) {
				if(region_protected) 
					region_protect(region);
				region_unlock(region);
				subreg_unlock(subreg);
				return err;
			}
			subreg->actual_host = 1;
		}
		
		// need to copy from host to this device
		if(err = subreg_link_sync_to_device(subreg, host_array->links[idev])) {
			if(region_protected)
				region_protect(region);
			region_unlock(region);
			subreg_unlock(subreg);
			return err;
		}
		subreg->actual_device = idev;
		subreg->actual_mask |= 1ul << idev;

		// restore protection state & locks
		if(region_protected) {
			if(err = region_protect(region)) {
				region_unlock(region);
				subreg_unlock(subreg);
				return err;
			}
		}
		
		if(err = region_unlock(region)) {
			subreg_unlock(subreg);
			return err;
		}
		region_unlock(region);
	}  // if already on device
	if(err = subreg_unlock(subreg))
		return err;
	return 0;
}  // subreg_sync_to_host

int subreg_sync_to_host(subreg_t *subreg) {
	int err;
	//if(subreg->actual_host)
	//	return 0;
	if(err = subreg_lock(subreg))
		return err;
	
	// check if already on host
	if(!subreg->actual_host) {
		// have to copy from actual device
		unsigned idev = subreg->actual_device;
		host_array_t *host_array = subreg->host_array;

		// lock region
		//fprintf(stderr, "locking region\n");
		region_t *region = subreg->region;
		if(err = region_lock(region)) {
			subreg_unlock(subreg);
			return err;
		}

		// remove region protection of necessary
		int region_protected = region_is_protected(region);
		if(region_protected) {
			if(err = region_unprotect(region)) {
				region_unlock(region);
				subreg_unlock(subreg);
				return err;
			}
		}

		// do actualy copying
		//fprintf(stderr, "doing copying\n");
		if(err = subreg_link_sync_to_host(subreg, host_array->links[idev])) {
			if(region_protected) 
				region_unprotect(region);
			region_unlock(region);
			subreg_unlock(subreg);
			return err;
		}
		// if anything is copied to host, device state loses actuality
		subreg->actual_host = 1;
		subreg->actual_device = NO_ACTUAL_DEVICE;
		subreg->actual_mask = 0ul;
		
		// remove locks and return protection
		//fprintf(stderr, "setting protection back if needed\n");
		if(region_protected) {
			if(err = region_protect(region)) {
				region_unlock(region);
				subreg_unlock(subreg);
				return err;
			}
		}  // if(protected)

		//fprintf(stderr, "unlocking region\n");
		if(err = region_unlock(region)) {
			subreg_unlock(subreg);
			return err;
		}
	}
	
	//fprintf(stderr, "unlocking subregion\n");
	if(err = subreg_unlock(subreg))
		return err;
	return 0;
}  // subreg_sync_to_host

int subreg_after_kernel(subreg_t *subreg, unsigned idev) {
	// lock subregion
	int err;
	if(err = subreg_lock(subreg))
		return err;

	// update subregion actuality
	subreg->actual_host = 0;
	subreg->actual_device = idev;
	subreg->actual_mask = 1ul << idev;

	// lock region
	region_t *region = subreg->region;
	if(err = region_lock(region)) {
		subreg_unlock(subreg);
		return err;
	}

	// turn on region memory protection
	if(err = region_protect(region)) {
		region_unlock(region);
		subreg_unlock(subreg);
		return err;
	}

	// unlock region
	if(err = region_unlock(region)) {
		subreg_unlock(subreg);
		return err;
	}

	// unlock subregion
	if(err = subreg_unlock(subreg))
		return err;
	return 0;
}  // subreg_after_kernel
