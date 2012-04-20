/** @file sync.c 
		this file contains implementation of global reader/writer lock synchronization
 */

#include <pthread.h>
#include <signal.h>
#include <stdio.h>

#include "gpuvm.h"
#include "stat.h"
#include "util.h"

/** a helper signal mask to (un)block during writer lock */
sigset_t writer_block_sig_g;

/** global mutex lock */
pthread_rwlock_t mutex_g;

int sync_init(void) {
	if(pthread_rwlock_init(&mutex_g, 0)) {
		fprintf(stderr, "sync_init: can\'t init rwlock\n");
		return GPUVM_ERROR;
	}
	sigemptyset(&writer_block_sig_g);
	sigaddset(&writer_block_sig_g, SIG_MONOGC_SUSPEND);
#ifndef __APPLE__
	sigaddset(&writer_block_sig_g, SIG_SUSP);
#endif
	return 0;
}

int lock_reader(void) {
	fprintf(stderr, "locking reader\n");
	if(pthread_rwlock_rdlock(&mutex_g)) {
		fprintf(stderr, "lock_reader: reader can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int lock_writer(void) {
	fprintf(stderr, "locking writer\n");
	if(stat_writer_sig_block())
		sigprocmask(SIG_BLOCK, &writer_block_sig_g, 0);
	if(pthread_rwlock_wrlock(&mutex_g)) {
		fprintf(stderr, "lock_writer: writer can\'t lock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int unlock_reader(void) {
	fprintf(stderr, "unlocking reader\n");
	if(pthread_rwlock_unlock(&mutex_g)) {
		fprintf(stderr, "unlock_reader: reader unlock\n");
		return GPUVM_ERROR;
	}
	return 0;
}

int unlock_writer(void) {
	fprintf(stderr, "unlocking writer\n");
	if(pthread_rwlock_unlock(&mutex_g)) {
		fprintf(stderr, "unlock_writer: reader unlock\n");
		return GPUVM_ERROR;
	}
	if(stat_writer_sig_block())
		sigprocmask(SIG_UNBLOCK, &writer_block_sig_g, 0);
	return 0;
}
