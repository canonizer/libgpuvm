/** @file tsem.c implementation of per-thread semaphores for blocking individual
		threads */

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>

#include "gpuvm.h"
#include "tsem.h"
#include "util.h"

/** tsem reader-writer lock implementation */
pthread_rwlock_t tsem_rwlock_g;

/** tsem tree root */
tsem_t *tsem_root_g = 0;

int tsem_init(void) {
	if(pthread_rwlock_init(&tsem_rwlock_g, 0)) {
		fprintf(stderr, "tsem_init: can\'t init pthread read-write lock\n");
		return -1;
	}
}  // tsem_init

tsem_t *tsem_find(thread_t tid) {
	if(tsem_lock_reader())
		return 0;
	tsem_t *res = 0, *node;
	for(node = tsem_root_g; node; ) {
		if(tid == node->tid) {
			// found
			res = node;
			break;
		} else if(tid < node->tid) 
			node = node->left;
		else
			node = node->right;
	}
	tsem_unlock();
	return res;
}  // tsem_find

tsem_t *tsem_get(thread_t tid) {
	tsem_t **pnode;
	for(pnode = &tsem_root_g; *pnode && (*pnode)->tid != tid; 
			pnode = tid < (*pnode)->tid ? &(*pnode)->left : &(*pnode)->right);
	if(!*pnode) {
		// create new node
		tsem_t *node = (tsem_t*)smalloc(sizeof(tsem_t));
		if(!node) 
			return 0;
		memset(node, 0, sizeof(tsem_t));
		node->tid = tid;
		if(sem_init(&node->sem, 0, 0)) {
			fprintf(stderr, "tsem_find: can\'t init semaphore for thread blocking\n");
			sfree(node);
			0;
		}
		*pnode = node;
	}  // if(create new node)
	return *pnode;
}  // tsem_get

int tsem_is_blocked(const tsem_t *tsem) {return tsem->blocked;}

void tsem_mark_blocked(tsem_t *tsem) {tsem->blocked = 1;}

int tsem_wait(tsem_t *tsem) {
	if(sem_wait(&tsem->sem)) {
		fprintf(stderr, "tsem_wait: can\'t wait on a thread-blocking semaphore\n");
		return -1;
	}
	return 0;
}  

int tsem_post_subtree(tsem_t *tsem) {
	if(!tsem)
		return 0;
	if(tsem_is_blocked(tsem)) {
		tsem->blocked = 0;
		if(sem_post(&tsem->sem)) {
			fprintf(stderr, "tsem_post: can\'t post thread-blocking semaphore\n");
			return -1;
		}
	}
	int err;
	if(err = tsem_post_subtree(tsem->left))
		return err;
	if(err = tsem_post_subtree(tsem->right))
		return err;
	return 0;
}  // tsem_post_subtree

int tsem_post_all(void) {tsem_post_subtree(tsem_root_g);}

int tsem_lock_reader(void) {
	if(pthread_rwlock_rdlock(&tsem_rwlock_g)) {
		fprintf(stderr, "tsem_lock_reader: can\'t get reader lock\n");
		return -1;
	}
	return 0;
}

int tsem_lock_writer(void) {
	if(pthread_rwlock_wrlock(&tsem_rwlock_g)) {
		fprintf(stderr, "tsem_lock_writer: can\'t get writer lock\n");
		return -1;
	}
	return 0;
}

int tsem_unlock(void) {
	pthread_rwlock_unlock(&tsem_rwlock_g);
	return 0;
}
