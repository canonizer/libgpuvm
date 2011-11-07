#ifndef _GPUVM_SUBREG_H_
#define _GPUVM_SUBREG_H_

/** @file subreg.h
		this file contains the definition of the subregion structure
 */

#include <pthread.h>
#include "util.h"

struct host_array_struct;
struct region_struct;

/** the mask indicating on which devices the subregion is actual and on which it is not */
typedef unsigned long long devmask_t;

/** constant meaning no actual device */
#define NO_ACTUAL_DEVICE (~0)

/** a subregion is an intersection of a region and a host array */
typedef struct subreg_struct {
	/** memory range of the subregion */
	memrange_t range;
	/** host array to which this subregion belongs */
	struct host_array_struct *host_array;
	/** region to which this subregion belongs */
	struct region_struct *region;
	/** indicates the device where host_array is actual, or, in case of multiple devices,
			the first such deivce; if there is none, must be #NO_ACTUAL_DEVICE */
	volatile unsigned actual_device;
	/** 1 if actual on host and 0 if not */
	volatile unsigned actual_host;
	/** the mask indicating on which devices the subregion is actual; bit 0 is for device 0,
			bit 1 for device 1 etc */
	volatile devmask_t actual_mask;
	/** the mutex to lock and unlock the region in a multithreaded environment */
	// pthread_mutex_t mutex;
} subreg_t;

/** allocates a new subregion. The subregion is initially assumed to be actual on host 
		@param p *p contains pointer to allocated subregion if successful and 0 if not
		@param hostptr the start address of the subregion
		@param nbytes the size of the subregion
		@param flags the device where the subreg is actual, or a negative value if
		it is actual on host
		@returns 0 if successful and a negative error code if not
 */
int subreg_alloc(subreg_t **p, void *hostptr, size_t nbytes, int idev);

/** removes the subregion from the region it belongs to and frees the subregion. Note that
		if the subregion is the last one in the region, then the region is removed as well */
void subreg_free(subreg_t *subreg);

/** synchronizes subregion to device 
		@param subreg the subregion to synchronize to device
		@param idev the device to which to synchronize
		@returns 0 if successful and a negative error code if not
 */
int subreg_sync_to_device(subreg_t *subreg, unsigned idev);

/** synchronizes subregion to host
		@param subreg the subregion to synchronize to host
		@returns 0 if successful and a negative error code if not
 */
int subreg_sync_to_host(subreg_t *subreg);

/** performs actions necessary after the subregion has been used in device kernel. This
		includes setting up memory protection and marking the subregion as valid only on the
		device it was used at 
		@param subreg the subregion which has been used on device
		@param idev the device on which the kernel has been executed
		@returns 0 if successful and a negative error code if not
*/
int subreg_after_kernel(subreg_t *subreg, unsigned idev);

#endif
