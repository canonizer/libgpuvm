/** @file salloc.c 
		implementation of special separate allocator. Currently, implementation simply
		"allocates" data from a pre-allocated buffer, and does not free them at all. This
		implementation is for testing purposes only, and is not intended to be used in production
*/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpuvm.h"
#include "util.h"

/** maximum allocation size, 4 MB */
const size_t MAX_BYTES = 4 * 1024 * 1024;

/** base pointer to blob from which data is allocated */
char *baseptr_g = 0;

/** number of bytes already allocated */
size_t allocd_g = 0;

/** request a data blob */
int salloc_init() {
	if(posix_memalign((void**)&baseptr_g, GPUVM_PAGE_SIZE, MAX_BYTES)) {
		fprintf(stderr, "init: can\'t allocate memory from system");
		baseptr_g = 0;
	}
	return baseptr_g ? 0 : GPUVM_ESALLOC;
}

void *smalloc(size_t nbytes) {

	// maintain alignment
	if(nbytes % SALIGN)
		nbytes += SALIGN - nbytes % SALIGN;

	// check if out of memory
	if(allocd_g + nbytes > MAX_BYTES) {
		fprintf(stderr, "smalloc: can\'t allocate memory\n");
		return 0;
	}	

	// do allocation
	void *resptr = baseptr_g + allocd_g;
	allocd_g += nbytes;
	return resptr;	
}

void sfree(void *ptr) {
	if(!ptr)
		return;
	char *cptr = (char*)ptr;
	if(cptr - baseptr_g < 0 || cptr - baseptr_g > allocd_g)
		fprintf(stderr, "sfree: invalid pointer");
	return;
}
