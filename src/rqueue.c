/** @file rqueue.c implementation of region queue operations */

#include <stdio.h>
#include <string.h>

#include "gpuvm.h"
#include "rqueue.h"

/** locks the queue 
		@param queue the queue to lock
		@returns 0 if successful and a negative error code if not
 */
static int rqueue_lock(rqueue_t *queue) {
	if(pthread_mutex_lock(&queue->mutex)) {
		fprintf(stderr, "rqueue_lock: can\'t lock mutex\n");
		return -1;
	}
	return 0;
}

/** unlocks the queue 
		@param queue the queue to unlock
		@returns 0 if successful and a negative error code if not
		@remarks unlocking is always successful
 */
static int rqueue_unlock(rqueue_t *queue) {
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

int rqueue_init(rqueue_t *queue, rqueue_elem_t *data, unsigned buffer_size) {
	memset(queue, 0, sizeof(rqueue_t));
	queue->data = data;
	queue->buffer_size = buffer_size;
	if(pthread_mutex_init(&queue->mutex, 0)) {
		fprintf(stderr, "rqueue_init: can\'t init mutex\n");
		return -1;
	}
	if(pthread_cond_init(&queue->non_empty_cond, 0)) {
		fprintf(stderr, "rqueue_init: can\'t init condition variable\n");
		pthread_mutex_destroy(&queue->mutex);
		return -1;
	}
	return 0;
} // rqueue_init

int rqueue_put(rqueue_t *queue, const rqueue_elem_t *elem) {
	if(rqueue_lock(queue))
		return -1;
	if((queue->tail + 1) % queue->buffer_size == queue->head) {
		// queue is full
		fprintf(stderr, "rqueue_put: can\'t put, queue is full\n");
		rqueue_unlock(queue);
		return -1;
	}
	int was_empty = queue->head == queue->tail;
	queue->data[queue->tail] = *elem;
	queue->tail = (queue->tail + 1) % queue->buffer_size;
	if(was_empty && pthread_cond_signal(&queue->non_empty_cond)) {
		fprintf(stderr, "rqueue_put: can\'t signal non-empty condition\n");
		rqueue_unlock(queue);
		return -1;
	}
	rqueue_unlock(queue);
}  // rqueue_put

int rqueue_get(rqueue_t *queue, rqueue_elem_t *elem) {
	if(rqueue_lock(queue))
		return -1;

	if(queue->tail == queue->head) {
		// wait for element availability
		if(pthread_cond_wait(&queue->non_empty_cond, &queue->mutex)) {
			fprintf(stderr, "rqueue_get: can\'t lock non-empty indicator mutex\n");
			return -1;
		}
	}  // if(queue is empty)

	*elem = queue->data[queue->head];
	queue->head = (queue->head + 1) % queue->buffer_size;
	rqueue_unlock(queue);
}  // rqueue_get
