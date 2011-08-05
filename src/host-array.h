#ifndef _GPUVM_HOST_ARRAY_H_
#define _GPUVM_HOST_ARRAY_H_

/** @file host-array.h
		this file contains definition for the host_array_t structure, which corresponds to a
		single host array, and holds data on that array as well as pointers to links and
		subregions associated with array
 */

#include "util.h"

#define MAX_SUBREGS 3

struct link_struct;
struct subreg_struct;

typedef struct host_array_struct {
	/** memory range corresponding to the array */
	memrange_t range;
	/** links associated with the host array, one for each device */
	struct link_struct **links;
	/** total number of subregions associated with array, no more than MAX_SUBREGS */
	unsigned nsubregs;
	/** subregions associated with the array; only first nsubregs point to actual
	subregions, others are null */
	struct subreg_struct *subregs[MAX_SUBREGS];
} host_array_t;

/** allocates the host array, under assumption that no such array exists. Subregions are
		allocated as well, as are regions, if necessary
		@param p [out] *p points to allocated array if successful and is 0 if not
		@param hostptr pointer to the start of the host array
		@param nbytes size of the host array in bytes
		@param flags array flags, currently must be GPUVM_OPENCL | GPUVM_ON_DEVICE
		@returns 0 if successful and negative error code if not
 */
int host_array_alloc(host_array_t **p, void *hostptr, size_t nbytes, int flags);

/** frees a previously allocated host array 
		@param host_array host array to free
 */
void host_array_free(host_array_t *host_array);

/** finds an array which either equals or intersects the specified range
		@param p [out] *p contains pointer to array if found and 0 if not
		@param hostptr start of memory range
		@param nbytes the size of memory range; may be 0 if searching for pointer only
		@returns 0 if either no array if found or the array found is the same as the host
		range, and 1 if the array found is not the same as the host range
 */
int host_array_find(host_array_t **p, void *hostptr, size_t nbytes);

/** synchronizes the array (if necessary) so that it is actual on the specified device
		@param the array to synchronize
		@param idev the device on which to make the array synchronous
		@return 0 if successful and a negative error code if not
 */
int host_array_sync_to_device(host_array_t *host_array, unsigned idev);

/** performs necessary actions after device counterpart of the array has been used in the
		kernel (for both reading and writing). This includes marking array as not-actual on
		host and setting up memory protection
		@param host_array the array which was used on device
		@param idev the device on which the array was used
 */
int host_array_after_kernel(host_array_t *host_array, unsigned idev);

#endif
