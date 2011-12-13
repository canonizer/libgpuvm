#ifndef GPUVM_TSEM_H_
#define GPUVM_TSEM_H_

/** @file tsem.h. interface for per-thread semaphores for blocking individual threads
 */
#include <semaphore.h>

#include "gpuvm.h"
#include "util.h"

typedef struct tsem_struct {
	/** the (integer) thread id to which the semaphore belongs */
	thread_t tid;
	/** the semaphore used to block the thread */
	//sem_t sem;
	/** the mutex on which the thread blocks */
	pthread_mutex_t mut;
	/** left and right subtrees, to hold data for other threads */
	struct tsem_struct *left, *right;
	/** whether the thread was blocked */
	int blocked;
} tsem_t;

/** finds the tsem belonging to a thread with a specific id. This a
		reader-locking call
		@param tid the id of the thread for which to find the tsem
		@returns the tsem found, or 0 if none; if no tsem exists for a thread, no
		attempt is made to create it
 */
tsem_t *tsem_find(thread_t tid);

/** gets the tsem for a thread, and creates a new one if none is found. This is
		a non-locking call, and because it can create a new tsem, a writer lock is required
		@param tid the id of the thread for which to find or create the tsem
		@returns the tsem found or created; 0 if there was an error in finding or
		creating a tsem
 */
tsem_t *tsem_get(thread_t tid);

/** checks whether tsem is blocked 
		@param tsem the tsem to check
		@returns non-zero if blocked and 0 if not
 */
int tsem_is_blocked(const tsem_t *tsem);

/** marks the tsem as blocked. This method always succeeds
		@param tsem the tsem to mark as blocked
 */
void tsem_mark_blocked(tsem_t *tsem);

/** called by a thread to wait the tsem. This call does not require any
		synchronization
		@param tsem the thread semaphore on which to wait
		@returns 0 if successful and a negative error code if not
 */
int tsem_wait(tsem_t *tsem);

/** called by the stopping thread so that other threads can stop on this
		semaphore
		@param tsem the thread semaphore on which to wait
		@returns 0 if successful and a negative error code if not
 */
int tsem_pre_stop(tsem_t *tsem);

/** posts to all blocked tsems, unlocking all blocked threads. This call does
		not require any synchronization, as it is called by unprot thread only
		@returns 0 if successful and a negative error code if not
 */
int tsem_post_all(void);

/** traverses all tsems, and calls a function on each tsem 
		@param f the function to call on each tsem, accepts a tsem and returns an
		error code
		@returns 0 if successful with all nodes and a negative error code if not; in
		case of an error, traversal of all subnodes is not guaranteed
 */
int tsem_traverse_all(int (*f)(tsem_t*));

/** initializes tsem-related infrastructure 
		@returns 0 if successful and a negative error code if not
 */
int tsem_init(void);

/** reader lock on tsem infrastructure 
		@returns 0 if successful and a negative error code if not
 */
int tsem_lock_reader(void);

/** writer lock on tsem infrastructure 
		@returns 0 if successful and a negative error code if not
 */
int tsem_lock_writer(void);

/** remove any lock on tsem infrastructure 
		@returns 0 if successful and a negative error code if not
 */
int tsem_unlock(void);

#endif
