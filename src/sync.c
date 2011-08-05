/** @file sync.c 
		this file contains implementation of global reader/writer lock synchronization
 */

#include <pthread.h>
#include <stdio.h>

#include "gpuvm.h"
#include "util.h"

/** global reader-writer lock */
pthread_rwlock_t rwlock;

int sync_init() {
	if(pthread_rwlock_init(&rwlock, 0)) {
		fprintf(stderr, "sync_init: can\'t init reader-writer lock");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_reader() {
	if(pthread_rwlock_rdlock(&rwlock)) {
		fprintf(stderr, "lock_reader: reader can\'t lock");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_writer() {
	if(pthread_rwlock_wrlock(&rwlock)) {
		fprintf(stderr, "lock_writer: writer can\'t lock");
		return GPUVM_ERROR;
	}
	return 0;
}

int sync_unlock() {
	if(pthread_rwlock_unlock(&rwlock)) {
		fprintf(stderr, "sync_unlock: reader unlock");
		return GPUVM_ERROR;
	}
	return 0;
}
