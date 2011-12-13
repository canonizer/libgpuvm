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

double time_diff(const struct timeval *start, const struct timeval *end) {
	return (end->tv_sec - start->tv_sec) + 
		1e-6 * (end->tv_usec - start->tv_usec);
}
