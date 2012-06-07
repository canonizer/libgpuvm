/** @file util.c 
		Implementation of general utility functions (OS-independent)
 */

#include <stdio.h>
#include <stdlib.h>

#include "gpuvm.h"
#include "util.h"

thread_t immune_threads_g[MAX_NTHREADS];
unsigned immune_nthreads_g = 0;

int threads_diff(thread_t **prthreads, thread_t *athreads, unsigned anthreads, 
								 thread_t *bthreads, unsigned bnthreads) {
	thread_t *rthreads = (thread_t*)malloc(anthreads * sizeof(thread_t));
	if(!rthreads) {
		fprintf(stderr, "threads_diff: can\'t allocate memory for " 
						"resulting threads\n");
		return -1;
	}
	// compute difference using trivial algorithm
	unsigned rnthreads = 0, aithread, bithread;
	for(aithread = 0; aithread < anthreads; aithread++) {
		int thread_found = 0;
		for(bithread = 0; bithread < bnthreads; bithread++)
			if(athreads[aithread] == bthreads[bithread]) {
				thread_found = 1;
				break;
			}
		if(!thread_found)
			rthreads[rnthreads++] = athreads[aithread];
	}
	*prthreads = rthreads;
	return rnthreads;
}  // threads_diff

double rtime_diff(const rtime_t *start, const rtime_t *end) {
#ifdef GPUVM_CLOCK_GETTIME
	return (end->tv_sec - start->tv_sec) + 
		1e-9 * (end->tv_nsec - start->tv_nsec);
#else
	return (end->tv_sec - start->tv_sec) + 
		1e-6 * (end->tv_usec - start->tv_usec);
#endif
}

rtime_t rtime_get() {
	rtime_t rt;
#ifdef GPUVM_CLOCK_GETTIME
	clock_gettime(CLOCK_MONOTONIC, &rt);
#else
	gettimeofday(&rt, 0);
#endif
	return rt;
}

memrange_cmp_t memrange_cmp
(const memrange_t* a, const memrange_t*	b) {
	//fprintf(stderr, "comparing memory ranges a = %p and b = %p\n", a, b);
	if((char*)a->ptr + a->nbytes <= (char*)b->ptr)
		return MR_CMP_LT;
	if((char*)b->ptr + b->nbytes <= (char*)a->ptr)
		return MR_CMP_GT;
	if(a->ptr == b->ptr && a->nbytes == b->nbytes)
		return MR_CMP_EQ;
	return MR_CMP_INT;
}

int memrange_is_inside(const memrange_t* a, const memrange_t* b) {
	return (char*)a->ptr <= (char*)b->ptr && 
		(char*)a->ptr + a->nbytes >= (char*)b->ptr + b->nbytes;
}

memrange_cmp_t memrange_pos_ptr(const memrange_t* range, const void *aptr) {
	if((char*)aptr < (char*)range->ptr)
		return MR_CMP_LT;
	else if((char*)aptr < (char*)range->ptr + range->nbytes)
		return MR_CMP_INT;
	else
		return MR_CMP_GT;
}
