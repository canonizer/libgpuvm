#ifndef GPUVM_WTHREADS_H_
#define GPUVM_WTHREADS_H_
/** @file wthreads.h interface to GPUVM worker threads */

struct region_struct;

/** initializes GPUVM worker threads 
		@returns 0 if successful and a negative error code if not
 */
int wthreads_init();

/** puts a region for wthread handling 
		@param region the region to put for handling
		(typically protection removal)
*/
void wthreads_put_region(struct region_struct *region);

#endif
