/** @file salloc.c 
		implementation of special separate allocator. Currently, implementation simply
		"allocates" data from a pre-allocated buffer, and does not free them at all. This
		implementation is for testing purposes only, and is not intended to be used in production
*/

#include <malloc.h>
#include <stddef.h>
#include <stdio.h>

#include "gpuvm.h"
#include "util.h"

/** maximum allocation size, 4 MB */
const size_t MAX_BYTES = 4 * 1024 * 1024;

/** base pointer to blob from which data is allocated */
char *baseptr = 0;

/** number of bytes already allocated */
size_t allocd = 0;

/** request a data blob */
int salloc_init() {
	baseptr = (char*)memalign(GPUVM_PAGE_SIZE, MAX_BYTES);
	if(!baseptr)
		fprintf(stderr, "init: can\'t allocate memory from system");
	return baseptr ? 0 : GPUVM_ESALLOC;
}

void *smalloc(size_t nbytes) {

	// maintain alignment
	if(nbytes % SALIGN)
		nbytes += SALIGN - nbytes % SALIGN;

	// check if out of memory
	if(allocd + nbytes > MAX_BYTES) {
		fprintf(stderr, "smalloc: can\'t allocate memory\n");
		return 0;
	}	

	// do allocation
	void *resptr = baseptr + allocd;
	allocd += nbytes;
	return resptr;	
}

void sfree(void *ptr) {
	if(!ptr)
		return;
	char *cptr = (char*)ptr;
	if(cptr - baseptr < 0 || cptr - baseptr > allocd)
		fprintf(stderr, "sfree: invalid pointer");
	return;
}
