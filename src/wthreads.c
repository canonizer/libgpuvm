/** @file wthreads.c implementation of GPUVM worker threads */

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "gpuvm.h"
#include "region.h"
#include "rqueue.h"
#include "stat.h"
#include "subreg.h"
#include "util.h"
#include "wthreads.h"

/** maximum queue buffer size, in terms of numbers of elements */
#define MAX_QUEUE_SIZE 128

/** buffers for queues */
rqueue_elem_t unprot_queue_data_g[MAX_QUEUE_SIZE], 
	sync_queue_data_g[MAX_QUEUE_SIZE];

/** region queues for unprotecting and syncing regions, respectively */
rqueue_t unprot_queue_g, sync_queue_g;

/** ids of unprot and sync thread */
volatile thread_t unprot_thread_g, sync_thread_g;

/** initialization semaphore for GPUVM threads threads*/
sem_t init_sem_g;

/** thread routine prototypes */
static void *unprot_thread(void*);
static void *sync_thread(void*);

/** finishes the thread by sending a quit message */
static void wthread_quit(rqueue_t *queue) {
	rqueue_elem_t elem;
	elem.op = REGION_OP_QUIT;
	elem.region = 0;
	rqueue_put(queue, &elem);
}  // wthread_quit

/** quits unprot thread */
static void unprot_quit(void) {
	wthread_quit(&unprot_queue_g);
}

/** quits sync thread */
static void sync_quit(void) {
	wthread_quit(&sync_queue_g);
}

int wthreads_init() {
	// create queues for working threads
	int err;
	if(err = rqueue_init(&unprot_queue_g, unprot_queue_data_g, MAX_QUEUE_SIZE)) 
		return err;
	if(err = rqueue_init(&sync_queue_g, sync_queue_data_g, MAX_QUEUE_SIZE)) 
		return err;

	// start working threads
	if(sem_init(&init_sem_g, 0, 0)) {
		fprintf(stderr, "wthread_init: can\'t init thread init semaphore\n");
		return -1;
	}
	pthread_t dummy_pthread;
	if(pthread_create(&dummy_pthread, 0, unprot_thread, 0)) {
		fprintf(stderr, "wthread_init: can\'t start unprot thread\n");
		return -1;
	}
	if(pthread_create(&dummy_pthread, 0, sync_thread, 0)) {
		fprintf(stderr, "wthread_init: can\'t start sync thread\n");
		unprot_quit();
		return -1;
	}
	// set exit handlers
	if(sem_wait(&init_sem_g) || sem_wait(&init_sem_g) ||
		 atexit(unprot_quit) || atexit(sync_quit)) {
		fprintf(stderr, "wthread_init: can\'t finish initialization\n");
		unprot_quit();
		sync_quit();
	}
	// add to immute threads
	if(immune_nthreads_g + 2 > MAX_NTHREADS) {
		fprintf(stderr, "wthread_init: too many immune threads\n");
		unprot_quit();
		sync_quit();
	}
	immune_threads_g[immune_nthreads_g++] = unprot_thread_g;
	immune_threads_g[immune_nthreads_g++] = sync_thread_g;

	// destroy initialization semaphore
	sem_destroy(&init_sem_g);

	return 0;
}  // wthread_init

void wthreads_put_region(region_t *region) {
	rqueue_elem_t elem;
	elem.region = region;
	elem.op = REGION_OP_UNPROTECT;
	rqueue_put(&unprot_queue_g, &elem);
} 

/** thread routine for the thread which does unprotection of regions */
static void *unprot_thread(void *dummy_param) {
	unprot_thread_g = self_thread();
	if(sem_post(&init_sem_g)) {
		fprintf(stderr, "unprot_thread: can\'t post init semaphore\n");
		return 0;
	}
	rqueue_elem_t elem;
	// the number of regions which have been unprotected, but have not yet been
	// synced to host
	unsigned pending_regions = 0;
	// starting and ending time for this time period
	rtime_t start_time, end_time;
	while(1) {
		rqueue_get(&unprot_queue_g, &elem);
		region_t *region = elem.region;
		switch(elem.op) {

		case REGION_OP_QUIT:
			// quit the thread
			return 0;

		case REGION_OP_UNPROTECT:
			stat_inc(GPUVM_STAT_PAGEFAULTS);
			// remove protection, stop threads if necessary
			if(!pending_regions) {
				if(stat_enabled())
					start_time = rtime_get();
				//fprintf(stderr, "stopping other threads\n");
				stop_other_threads();
			}
			if(region_is_protected(region))
				region_unprotect(region);
			region_post_unprotect(region);
			
			pending_regions++;
			elem.op = REGION_OP_SYNC_TO_HOST;
			rqueue_put(&sync_queue_g, &elem);
			break;

		case REGION_OP_SYNCED_TO_HOST:
			pending_regions--;
			if(!pending_regions) {			 
				//fprintf(stderr, "continuing other threads\n");
				cont_other_threads();
				if(stat_enabled()) {
					end_time = rtime_get();
					stat_acc_unblocked_double(GPUVM_STAT_PAGEFAULT_TIME, 
													rtime_diff(&start_time, &end_time));
				}
			}  // if(!pending_regions)
			break;

		default:
			fprintf(stderr, "unprot_thread: invalid region operation %d\n", elem.op);
			break;
		}  // switch(elem->op)

	}  // while()
}  // unprot_thread()

/** thread routine for the thread which syncs subregions to host */
static void *sync_thread(void *dummy_param) {
	sync_thread_g = self_thread();
	if(sem_post(&init_sem_g)) {
		fprintf(stderr, "sync_thread: can\'t post init semaphore\n");
		return 0;
	}

	rqueue_elem_t elem;
	subreg_list_t *list;
	while(1) {
		rqueue_get(&sync_queue_g, &elem);
		region_t *region = elem.region;
		switch(elem.op) {

		case REGION_OP_QUIT:
			// quit the thread
			return 0;

		case REGION_OP_SYNC_TO_HOST:
			// sync region to host
			for(list = region->subreg_list; list; list = list->next)
				subreg_sync_to_host(list->subreg);
			
			elem.op = REGION_OP_SYNCED_TO_HOST;
			rqueue_put(&unprot_queue_g, &elem);
			break;

		default:
			fprintf(stderr, "sync_thread: invalid region operation %d\n", elem.op);
			break;
		}  // switch(elem->op)

	}  // while()
}  // unprot_thread()
