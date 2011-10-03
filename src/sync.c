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
pthread_mutex_t mutex_g;

int sync_init() {
	pthread_mutexattr_t attr;
	if(pthread_mutexattr_init(&attr)) {
		fprintf(stderr, "sync_init: can\'t init mutex attribute\n");
		return GPUVM_ERROR;
	}
	if(pthread_mutexattr_settype(&attr, RECURSIVE_MUTEX_ATTR)) {
		fprintf(stderr, "sync_init: can\'t set  mutex attribute type\n");
		pthread_mutexattr_destroy(&attr);
		return GPUVM_ERROR;
	}
	if(pthread_mutex_init(&mutex_g, &attr)) {
		fprintf(stderr, "sync_init: can\'t init mutex\n");
		pthread_mutexattr_destroy(&attr);
		return GPUVM_ERROR;
	}
	pthread_mutexattr_destroy(&attr);
	return 0;
}

int lock_reader() {
	if(pthread_mutex_lock(&mutex_g)) {
		fprintf(stderr, "lock_reader: reader can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_writer() {
	if(pthread_mutex_lock(&mutex_g)) {
		fprintf(stderr, "lock_writer: writer can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int sync_unlock() {
	if(pthread_mutex_unlock(&mutex_g)) {
		fprintf(stderr, "sync_unlock: reader unlock\n");
		return GPUVM_ERROR;
	}
	return 0;
}
