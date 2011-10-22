/** @file sync.c 
		this file contains implementation of global reader/writer lock synchronization
 */

#include <pthread.h>
#include <stdio.h>

#include "gpuvm.h"
#include "util.h"

#ifndef __APPLE__
#define RECURSIVE_MUTEX_ATTR PTHREAD_MUTEX_RECURSIVE_NP
#else
#define RECURSIVE_MUTEX_ATTR PTHREAD_MUTEX_RECURSIVE
#endif

/** global mutex lock */
pthread_rwlock_t mutex_g;

int sync_init() {
	if(pthread_rwlock_init(&mutex_g, 0)) {
		fprintf(stderr, "sync_init: can\'t init rwlock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_reader() {
	if(pthread_rwlock_rdlock(&mutex_g)) {
		fprintf(stderr, "lock_reader: reader can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_writer() {
	if(pthread_rwlock_wrlock(&mutex_g)) {
		fprintf(stderr, "lock_writer: writer can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int sync_unlock() {
	if(pthread_rwlock_unlock(&mutex_g)) {
		fprintf(stderr, "sync_unlock: reader unlock\n");
		return GPUVM_ERROR;
	}
	return 0;
}
