/** @file salloc.c 
		implementation of special separate allocator. Current allocator implementation is
		simple and working, yet it can take a long time for either allocation or
		deallocation. Memory is requested from OS in page-aligned regions of fixed size; all
		regions form a linked list, with a pointer to the initial region stored in a global
		variable. Each region maintains the count of allocated bytes and pointer to the next
		region. Regions are subdivided into blocks. For each request, a single block
		satisfying the request is found and then split into the portion allocated and the
		remaining free portion. Each free memory block maintains its size and pointer to the
		next block; each allocated block contains size only in prefix to its header. When an
		allocated block is freed, it is inserted into free memory list of the region, which is
		maintained in sorted order, and coalesced with its neighbours if it is possible. The
		allocator can be used by multiple threads simultaneously; however, it is not
		thread-safe, so only one thread can be inside the methods of the allocator at any
		given time
*/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "gpuvm.h"
#include "util.h"

/** block header */
typedef struct block_header_s {
	/** the size of the block, _including_ the block itself; maintained even after the block is allocated */
	size_t size;
	/** pointer to the next block, or NULL if none */
	struct block_header_s *next;
} block_header_t;

/** minimum alignment of allocated memory, bytes */
#define MIN_ALIGN 8

/** sizes of blocks requested from OS, in pages */
#define OS_BLOCK_PAGES 16

/** sizes of blocks requested from OS, in bytes */
#define OS_BLOCK_SIZE (OS_BLOCK_PAGES * GPUVM_PAGE_SIZE)

/** maximum allocation size allowed */
#define MAX_ALLOC_SIZE (GPUVM_PAGE_SIZE - sizeof(block_header_t))

/** ratio of maximum number of pages "held" without being freed to pages in OS-requested
		block */
#define MAX_HOLD_RATIO 4

/** maximum number of pages "held" without returning them back to OS */
#define MAX_HOLD_PAGES (MAX_HOLD_RATIO * OS_BLOCK_PAGES)

/** the value written to next field of an allocated block, checked on free */
#define ALLOC_CANARY 0xabababababababab /* (~(ptrdiff_t)0)*/

#ifndef __APPLE__
#define ANONYMOUS_MAP_FLAG MAP_ANONYMOUS
#else
#define ANONYMOUS_MAP_FLAG MAP_ANON
#endif

/** number of free pages currently being held */
size_t npages_held_g = 0;

/** start of block list */
block_header_t *block_list_g = 0;

/** dumps the list of free blocks; for debug use only */
static void dump_free_blocks(void) {
	block_header_t *block;
	for(block = block_list_g; block = block->next; block) {
		fprintf(stderr, "{%p, %zd}%s", block, block->size, block->next ? "->" : "\n");
	}
}  // dump_free_blocks()

/** 
		gets blocks from OS and inserts them into sorted list of free blocks
		@returns pointer to first allocated block if successful and NULL if not
 */
static block_header_t *alloc_os_blocks(void) {
	// allocate raw memory
	char *raw = (char*)mmap(0, OS_BLOCK_SIZE, PROT_READ | PROT_WRITE, 
									 MAP_PRIVATE | ANONYMOUS_MAP_FLAG, -1, 0);
	if(!raw) {
		fprintf(stderr, "alloc_os_blocks: can\'t get blocks from OS\n");
		return 0;
	}
	npages_held_g += OS_BLOCK_PAGES;

	// initialize block data structure
	int iblock;
	for(iblock = 0; iblock < OS_BLOCK_PAGES; iblock++) {
		block_header_t *block = (block_header_t*)(void*)(raw + iblock * GPUVM_PAGE_SIZE);
		block->size = GPUVM_PAGE_SIZE;
		block->next = iblock == OS_BLOCK_PAGES - 1 ? 0 : 
			(block_header_t*)(void*)(raw + (iblock + 1) * GPUVM_PAGE_SIZE);
	}
	block_header_t *first_new_block = (block_header_t*)(void*)raw;
	block_header_t *last_new_block = (block_header_t*)(void*)(raw + (OS_BLOCK_PAGES - 1) * GPUVM_PAGE_SIZE);
	
	// insert into sorted list of free blocks
	block_header_t **pblock;
	for(pblock = &block_list_g; *pblock && *pblock < first_new_block; pblock = &(*pblock)->next);
	last_new_block->next = *pblock;
	*pblock = first_new_block;
	return first_new_block;
}  // alloc_os_block()

int salloc_init(void) {
	if(!alloc_os_blocks()) 
		return GPUVM_ESALLOC;
	else
		return 0;
}  // salloc_init

void *smalloc(size_t nbytes) {
	// check size
	if(nbytes > MAX_ALLOC_SIZE) {
		fprintf(stderr, "smalloc: %zd bytes requested, greater than maximum "
						"allowed size %zd bytes\n", nbytes, MAX_ALLOC_SIZE);
		return 0;
	}

	// find siutable memory block
	size_t real_nbytes = nbytes + sizeof(block_header_t);
	int itry;
	for(itry = 0; itry < 2; itry++) {
		block_header_t **pblock;
		for(pblock = &block_list_g; *pblock && (*pblock)->size < real_nbytes; 
				pblock = &(*pblock)->next);
		if(*pblock) {
			// block found
			block_header_t *block = *pblock;
			size_t rblocks = real_nbytes / sizeof(block_header_t) + 
					(real_nbytes % sizeof(block_header_t) ? 1 : 0);
			size_t rsize = rblocks * sizeof(block_header_t);
			if(block->size - rsize >= 2 * sizeof(block_header_t)) {
				// split block
				block_header_t *new_block = block + rblocks;
				new_block->next = block->next;
				new_block->size = block->size - rsize;
				block->next = new_block;
				block->size = rsize;
			}
			// bypass block being allocated and return its free memory
			*pblock = block->next;
			block->next = (block_header_t*)ALLOC_CANARY;
			block_header_t *result = block + 1;
			memset(result, 0xcd, block->size - sizeof(block_header_t));
			//fprintf(stderr, "allocated %p\n", result);
			return result;
		} else if(!itry) { 
			if(!alloc_os_blocks())
				return 0;
		}
	}  // for(itry)
	// if here, no block can be allocated, and it's an error
	fprintf(stderr, "smalloc: can\'t allocate block after getting memory from OS; "
					"most likely an internal error\n");
	return 0;
}  // smalloc

/** frees blocks requested from OS */
static void free_os_blocks(void) {
	block_header_t **pblock;
	for(pblock = &block_list_g; *pblock && npages_held_g > MAX_HOLD_PAGES;) {
		if((*pblock)->size == GPUVM_PAGE_SIZE) {
			block_header_t *block = *pblock;
			*pblock = (*pblock)->next;
			if(munmap(*pblock, GPUVM_PAGE_SIZE))
				fprintf(stderr, "free_os_blocks: can\'t free OS page %p\n", block);
			npages_held_g--;
		} else
			pblock = &(*pblock)->next;
	}  // for(pblock)
}  // free_os_blocks

/** tries to coalesce two blocks; if coalescing is successful, the second block ceases to
		exist, and its portion of memory is added to the first block. Coalescing helps to reduce
		fragmentation. Only blocks belonging to a single page can be coalesced
		@param block1 the first block
		@param block2 the second block. Its address must be greater than that of the first block
*/
static void coalesce(block_header_t *block1, block_header_t *block2) {
	if(!block1 || !block2)
		return;
	if(block2 <= block1)
		return;
	if((block2 - block1) * sizeof(block_header_t) != block1->size)
		return;
	if((ptrdiff_t)block1 / GPUVM_PAGE_SIZE != (ptrdiff_t)block2 / GPUVM_PAGE_SIZE)
		return;
	//fprintf(stderr, "coalescing blocks {%p, %zd} and {%p, %zd}\n", block1, 
	//				block1->size, block2, block2->size);
	block1->size += block2->size;
	block1->next = block2->next;
	if(block1->size == GPUVM_PAGE_SIZE)
		npages_held_g++;
}  // coalesce

void sfree(void *ptr) {
	if(!ptr)
		return;

	//fprintf(stderr, "blocks before free:\n");
	//dump_free_blocks();
	
	// check canary
	block_header_t *block = (block_header_t*)ptr - 1;
	if((ptrdiff_t)block->next != ALLOC_CANARY) {
		fprintf(stderr, "sfree: invalid pointer %p passed to free\n", ptr);
		return;
	}
	if(block->size == GPUVM_PAGE_SIZE)
		npages_held_g++;

	// add to sorted free list
	/*block_header_t **pblock;
	for(pblock = &block_list_g; *pblock && *pblock < block; 
			pblock = &(*pblock)->next);
	block->next = *pblock;
	*pblock = block;*/
	// destroy block info
	//fprintf(stderr, "ptr = %p, block = %p, block size = %zd\n", ptr, block, block->size);
	memset(block + 1, 0xef, block->size - sizeof(block_header_t));
	//fprintf(stderr, "memory set");
	block_header_t *prev_block = 0, *next_block;
	for(next_block = block_list_g; next_block && next_block < block;
			prev_block = next_block, next_block = next_block->next);
	if(prev_block) 
		prev_block->next = block;
	else
		block_list_g = block;
	block->next = next_block;

	// coalesce
	//if(next_block)
	coalesce(block, next_block);
		 //if(prev_block)
	coalesce(prev_block, block);

	// free OS-allocated pages if necessary
	free_os_blocks();

	//fprintf(stderr, "blocks after free:\n");
	//dump_free_blocks();
	
	//fprintf(stderr, "freed %p\n", ptr);	
}  // sfree

// old allocation procedures

/** maximum allocation size, 4 MB */
//const size_t MAX_BYTES = 4 * 1024 * 1024;

/** base pointer to blob from which data is allocated */
//char *baseptr_g = 0;

/** number of bytes already allocated */
//size_t allocd_g = 0;

/*int salloc_init() {
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
*/
