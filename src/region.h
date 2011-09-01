#ifndef _GPUVM_REGION_H_
#define _GPUVM_REGION_H_

#include <pthread.h>
#include "util.h"

struct subreg_struct;

/** structure for the list of subregions */
typedef struct subreg_list_struct {
	struct subreg_struct *subreg;
	struct subreg_list_struct *next;
} subreg_list_t;

typedef struct region_struct {	
	/** memory range corresponding to this region; its start is aligned to page size, and
	its size is a multiple of page size */
	memrange_t range;
	/** current protection status of this memory region */
	int prot_status;
	/** total number of subregions */
	unsigned nsubregs;
	/** sorted list of subregions associated with this region */
	subreg_list_t *subreg_list;
	/** mutex to lock and unlock the region in a multithreaded environment */
	// pthread_mutex_t mutex;
} region_t;

/** allocates a new region which consists solely of the specified subregion. Also, assigns
		subregion to the region. Each newly allocated region is added to the region tree
		@param p [out] *p points to allocated region if successful and is 0 if not. p itself
		may be zero, in which case we'll get the pointer through subreg
		@param subreg the subregion of the region
		@returns 0 if successful and a negative error code if not
 */
int region_alloc(region_t **p, struct subreg_struct *subreg);

/** frees a region; all subregions must have been removed from the region prior to that */
void region_free(region_t *region);

/** turns on memory protection on the region 
		@param region memory region to protect. When global lock is reader, region lock must
		be acquired prior to that
		@returns 0 if successful and a negative error code if not
 */
int region_protect(region_t *region);

/** checks whether the region is protected 
		@param region region to check
		@returns nonzero if it is and 0 if it is not
 */
int region_is_protected(const region_t *region);

/** removes all memory protection from the region 
		@param region memory region to protect. When global lock is reader, region lock must
		be acquired prior to that
		@returns 0 if successful and a negative error code if not
 */
int region_unprotect(region_t *region);

/** adds a subregion to region
		@param region the region to which a subregion is being added
		@param subreg the subregion being added
		@returns 0 if successful and negative error code if not
 */
int region_add_subreg(region_t *region, struct subreg_struct *subreg);

/** removes a subregion from the region. If this is the last subregion in the region, the
		regions itself is removed. Removal of a subregion not in the region is silently ignored
		@param region the region from which a subregion is being removed
		@param subreg the subregion being removed
		@returns 0 if subregion is not in the region or if it is and is successfully removed
		and a negative error code if not
 */
int region_remove_subreg(region_t *region, struct subreg_struct *subreg);

/** finds the subregion of the region which contains the specified poitner 
		@param region the region in which a subregion is searched for
		@param ptr the pointer being searched for in the region
		@returns pointer to subregion containing ptr or 0 if none. This may happen, e.g.,
		because the pointer is outside the region, or there is no region 
 */
struct subreg_struct *region_find_subreg(const region_t *region, const void *ptr);

/** finds the region containing specific host address in the region tree 
		@param ptr the address to find
		@returns the region containing the pointer in question and 0 if none
 */
region_t *region_find_region(const void *ptr);

/** locks the region 
		@param region the region to lock
		@returns 0 if successful and a negative error code if not
 */
int region_lock(region_t *region);

/** unlocks the region 
		@param region the region to unlock
		@returns 0 if successful and a negative error code if not
 */
int region_unlock(region_t *region);

#endif
