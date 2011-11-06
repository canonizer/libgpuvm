#ifndef _GPUVM_UTIL_H_
#define _GPUVM_UTIL_H_

/** @file util.h
		this file contains interface to utility functions used by many parts of GPUVM
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
int salloc_init(void);

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
int lock_reader(void);

/** locks global lock for writer synchronization
 @returns 0 if successful and negative error code if not 
 */
int lock_writer(void);

/** unlocks global lock for reader synchronization 
 @returns 0 if successful and negative error code if not 
 */
int unlock_reader(void);

/** unlocks global lock for writer synchronization 
 @returns 0 if successful and negative error code if not 
 */
int unlock_writer(void);

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

/** "OS-independent OS-specific" thread type. Different values of this type correspond to
		different threads, and can be directly compared for (in)equality. Typically (though
		not guaranteed), it is an integer, which identifies the thread in OS */
#ifdef __APPLE__
#include <mach/mach.h>
typedef thread_port_t thread_t; 
#else
#include <sys/types.h>
typedef pid_t thread_t;
#endif

// maximum number of threads tracked (linux)
#define MAX_NTHREADS 256

/** "immune" threads, i.e. threads which must not be stopped during transfer */
extern thread_t immune_threads_g[MAX_NTHREADS];

/** number of "immune" threads */
extern unsigned immune_nthreads_g;

/** 
		gets OS-independent OS-specific thread identifier of the current thread
		@returns the thread identifier of the current thread
*/
thread_t self_thread();

/** 
		gets the list of OS-specific thread identifiers of the current
		process. Memory for the identifiers is allocated dynamically as
		needed. Therefore, this function can't be called from inside main libgpuvm
		code; it can only be called from pre-initialization phase
		@param [out] pthreads pointer to array of threads if successful, and undefined if
		not
		@returns the number of threads if successful and a negative error code if not
 */
int get_threads(thread_t **pthreads);

/** 
		computes difference between two thread sets 
		@param [out] prthreads pointer to the resulting set of threads, that 
		is, threads which are present in athreads and not present in bthreads 
		@param athreads the first set of threads 
		@param anthreads number of threads in the first set 
		@param bthreads	the second set of threads 
		@param bnthreads the number of threads in the	second set 
		@returns the number of threads in the difference set if	successful and 
		a negative error code if not
 */
int threads_diff(thread_t **prthreads, thread_t *athreads, unsigned anthreads, 
								 thread_t *bthreads, unsigned bnthreads);

/** 
		stops all threads except for the caller thread. Used to prevent false
		sharing errors between removing of memory protection and actualizing host
		buffer state. No errors are handled inside this function and no errors are
		returned, as it is always late to handle errors inside signal
		hander. Supported only on Linux or other OS with /proc FS, and on Darwin
		using Mach API
		@remarks on Darwin, currently a no-op, as it doesn't work well with Apple
		OpenCL implementation
 */
void stop_other_threads(void);

/** 
		resumes other threads, except for the caller, after they have been
		stopped. No errors are handled inside this function and no errors are
		returned, as it is always late to handle errors inside signal
		hander. Supported only on Linux or other OS with /proc FS, and on Darwin
		using Mach API
		@remarks on Darwin, currently a no-op, as it doesn't work well with Apple
		OpenCL implementation
 */
void cont_other_threads(void);

/** thread suspension signal number - for non-Darwin only*/
#ifndef __APPLE__
#define SIG_SUSP (SIGRTMIN + 4)
#endif

/** @} */

/** @{ */

/** total number of devices */
extern unsigned ndevs_g;

/** devices (queues in case of OpenCL) */
extern void **devs_g;

/** @} */

#endif
