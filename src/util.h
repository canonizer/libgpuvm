#ifndef _GPUVM_UTIL_H_
#define _GPUVM_UTIL_H_

/** @file util.h
		this file contains interface to utility functions used by many parts of GPVM
		implementation, such as special allocator, global synchronization etc. 

 */

#include <stddef.h>

/** 
		@{
		special separate allocator used inside GPUVM instead
		of the ordinary malloc()/free() combination. 

		A separate allocator is required to avoid
		additional memory faults during SIGSEGV handling
*/

/** memory alignment guaranteed by special separate allocator */
#define SALIGN 8

/** initializes special separate allocator
		@returns 0 if successful and negative value if not
*/
int salloc_init();

/** 
		allocates specific number of bytes
		@param nbytes number of bytes to allocate
		@returns pointer to allocated memory if successful and 0 if not
		@remarks the returned pointer is guaranteed to be aligned to 8 bytes. The function is
		not thread-safe.
 */
void *smalloc(size_t nbytes);

/** 
		frees memory allocated by salloc()
		@param ptr pointer to memory to be freed. Must be either a pointer previously
		allocated by salloc() and not freed after that, or 0. The function is not thread-safe.
 */
void sfree(void *ptr);

/** @} */

/** @{ 
		provides global data synchronization for threads accessing internal structures of
		GPUVM. Initialization must be done by one thread only and guaranteed by external
		means. gpuvm_link() and gpuvm_unlink() functions use write synchronization, while
		gpuvm_before_kernel() and gpuvm_after_kernel() use read synchronization
 */

/** initializes synchronization data structure 
		@returns 0 if successufl and negative error code if not 
 */
int sync_init();

/** locks global lock for reader synchronization 
 @returns 0 if successful and negative error code if not
 */
int lock_reader();

/** locks global lock for writer synchronization
 @returns 0 if successful and negative error code if not 
 */
int lock_writer();

/** unlocks global lock for reader or writer synchronization 
 @returns 0 if successful and negative error code if not 
 */
int sync_unlock();

/** @} */

/** @{ */
/** describes memory region with specific start and size */
typedef struct {
	/** start address of memory range (inclusive) */
	void *ptr;
	/** size of memory range, in bytes */
	size_t nbytes;
} memrange_t;

/** possible results of memory range comparison */
typedef enum {
	/** first range completely before the second one */
	MR_CMP_LT, 
	/** both ranges are equal */
	MR_CMP_EQ, 
	/** ranges intersect; for comparison with pointer means that the pointer lies within the
			range */
	MR_CMP_INT,
	/** second range is completely after the first one */
	MR_CMP_GT
} memrange_cmp_t;

/** compares two memory ranges 
		@param a first memory range
		@param b second memory range
		@returns appropriate memrage_cmp_t code
 */
static inline memrange_cmp_t memrange_cmp(const memrange_t* a, const memrange_t* b) {
	if((char*)a->ptr + a->nbytes <= (char*)b->ptr)
		return MR_CMP_LT;
	if((char*)b->ptr + b->nbytes <= (char*)a->ptr)
		return MR_CMP_GT;
	if(a->ptr == b->ptr && a->nbytes == b->nbytes)
		return MR_CMP_EQ;
	return MR_CMP_INT;
}

/** checks whether the second memrange is inside the first memrange 
		@param a the first memrange
		@param b the second memrange
		@returns nonzero value if it is and 0 if not
 */
static inline int memrange_is_inside(const memrange_t* a, const memrange_t* b) {
	return (char*)a->ptr <= (char*)b->ptr && 
		(char*)a->ptr + a->nbytes >= (char*)b->ptr + b->nbytes;
}

/** gets pointer position relative to the range 
		@param range memory range
		@param aptr pointer whose position is being checked
		@returns ::MR_CMP_LT if pointer lies before the range, ::MR_CMP_INT if it lies inside
		the range and ::MR_CMP_GT if it lies after the range
 */
static inline memrange_cmp_t memrange_pos_ptr(const memrange_t* range, const void *aptr)
{
	if((char*)aptr < (char*)range->ptr)
		return MR_CMP_LT;
	else if((char*)aptr < (char*)range->ptr + range->nbytes)
		return MR_CMP_INT;
	else
		return MR_CMP_GT;
}

/** @} */

/** @{ */

/** total number of devices */
extern unsigned ndevs_g;

/** devices (queues in case of OpenCL) */
extern void **devs_g;

/** @} */

#endif
