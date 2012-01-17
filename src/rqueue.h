#ifndef GPUVM_RQUEUE_H_
#define GPUVM_RQUEUE_H_

/** @file rqueue.h interface of inter-thread queue for region operations */

#include <pthread.h>

struct region_struct;

/** specifies either operation to be performed on region or response */
typedef enum {
	/** opertation ordering GPUVM worker thread to quit */
	REGION_OP_QUIT = 0,
	/** remove region protection */
	REGION_OP_UNPROTECT = 1,
	/** response to removing region protection; unused */
	REGION_OP_UNPROTECTED = 2,
	/** synchronizes region to host */
	REGION_OP_SYNC_TO_HOST = 3,
	/**  response to region synchronization to host */
	REGION_OP_SYNCED_TO_HOST = 4
} region_op_t;

/** region queue element */
typedef struct {
	/** a region on which to perform the operation */
	struct region_struct *region;
	/** region operation to perform */
	region_op_t op;	
} rqueue_elem_t;

/** region queue with one consumer (dequeuer) and several producers (enqueuers) */
typedef struct {
	/** the buffer for queue data */
	volatile rqueue_elem_t *data;
	/** the size of the buffer */
	unsigned buffer_size;
	/** tail, buffer index at which to insert the next element */
	volatile unsigned tail;
	/** head, buffer index at which to get the current element; if equal to tail,
			the queue is empty */
	volatile unsigned head;
	/** mutex controlling access to queue; must be locked for any queue operations
			*/
	pthread_mutex_t mutex;
	/** condition indicating availability of elements in the queue */
	pthread_cond_t non_empty_cond;
} rqueue_t;

/** 
		initializes a new region operation queue
		@param pqueue pointer to queue to initialize; note that the memory must
		already be allocated (statically or dynamically) for the queue
		@param data the buffer to be used by the queue; must already be allocated,
		most likely a global array of some kind
		@param buffer_size the size of the buffer
		@returns 0 if successful and a negative error code if not
 */
int rqueue_init(rqueue_t *queue, rqueue_elem_t *data, unsigned buffer_size);

/** puts an element into the queue 
		@param queue the queue into which to put the element
		@param elem the element to put
		@returns 0 if successful and a negative error code if not
		@remarks this is a non-blocking operation. It will fail if the queue is full
 */
int rqueue_put(rqueue_t *queue, const rqueue_elem_t *elem);

/** gets an element from the queue 
		@param queue the queue from which to get the element
		@param elem the element to get
		@returns 0 if successful and a negative error code if not
		@remarks this is a blocking operation. If the queue is empty, it will wait
		on the mutex for availability of elements
 */
int rqueue_get(rqueue_t *queue, rqueue_elem_t *elem);

/** locks the queue 
		@param queue the queue to lock
		@returns 0 if successful and a negative error code if not
 */
int rqueue_lock(rqueue_t *queue);

/** unlocks the queue 
		@param queue the queue to unlock
		@returns 0 if successful and a negative error code if not
		@remarks unlocking is always successful
 */
int rqueue_unlock(rqueue_t *queue);


#endif
