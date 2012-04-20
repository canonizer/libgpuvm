/** @file host-array.c implementation of host_array_t */

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "gpuvm.h"
#include "host-array.h"
#include "link.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

/** splits the range passed into 1-3 subranges based on page boundaries:
		if a range lies inside a single page, it is returned
		if the start of the range is not page-aligned, then first subrange is the part of the
		range lying in its starting page
		if the end of the range is not page-aligned, then the last subrange is the part of the
		range lying in its ending page
		the rest of the range forms the subrange in the middle with both its start and end
		page-aligned
		@param subranges [out] resulting subranges. Only first N subranges are valid, where N
		is the value returned by the function
		@param range pointer to the range to be split
		@returns the number of subranges into which the range is split
*/
static unsigned split_range(memrange_t subranges[MAX_SUBREGS], const memrange_t* range) {
	unsigned nsubranges;
	ptrdiff_t range_addr = (char*)range->ptr - (char*)0;
	subranges[0].ptr = (void*)range_addr;

	if(range_addr / GPUVM_PAGE_SIZE == (range_addr + range->nbytes - 1) / GPUVM_PAGE_SIZE) {
		// case 1: range is entirely within a single page, single subrange
		subranges[0].nbytes = range->nbytes;
		nsubranges = 1;
	} else if(range_addr % GPUVM_PAGE_SIZE == 0 && 
						(range_addr + range->nbytes) % GPUVM_PAGE_SIZE == 0) {
		// case 2: both beginning and the end are page-aligned, single subrange
		subranges[0].nbytes = range->nbytes;
		nsubranges = 1;
	} else if(range_addr / GPUVM_PAGE_SIZE + 1 == 
						(range_addr + range->nbytes - 1) / GPUVM_PAGE_SIZE) {
		// case 3: range is entirely within 2 pages, one of the ends not page-aligned, 
		// 2 subranges
		subranges[0].nbytes = (range_addr / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE -
	range_addr;
		subranges[1].ptr = (void*)((range_addr / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE);
		subranges[1].nbytes = (void*)range_addr + range->nbytes - subranges[1].ptr;
		nsubranges = 2;
	} else if(range_addr % GPUVM_PAGE_SIZE == 0) {
		// case 4: beginning is page-aligned, end is not page-aligned, 2 subranges
		subranges[0].nbytes = range->nbytes / GPUVM_PAGE_SIZE * GPUVM_PAGE_SIZE;
		subranges[1].ptr = (void*)(range_addr + subranges[0].nbytes);
		subranges[1].nbytes = range->nbytes - subranges[0].nbytes;
		nsubranges = 2;
	} else if((range_addr + range->nbytes) % GPUVM_PAGE_SIZE == 0) {
		// case 5: beginning is not page-aligned, end is page-aligned, 2 subranges
		subranges[0].nbytes = (range_addr / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE -
			range_addr;
		subranges[1].ptr = (void*)(range_addr + subranges[0].nbytes);
		subranges[1].nbytes = range->nbytes - subranges[0].nbytes;
		nsubranges = 2;
	} else {
		// case 6: neither beginning or end are page-aligned, with medium range, 
		// 3 subranges
		subranges[0].nbytes = (range_addr / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE -
			range_addr;
		subranges[1].ptr = (void*)((range_addr / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE);
		subranges[1].nbytes = (range_addr + range->nbytes) / GPUVM_PAGE_SIZE * GPUVM_PAGE_SIZE
			- (ptrdiff_t)subranges[1].ptr;
		subranges[2].ptr = (void*)((range_addr + range->nbytes) / GPUVM_PAGE_SIZE * GPUVM_PAGE_SIZE);
		subranges[2].nbytes = range->nbytes - subranges[1].nbytes - subranges[0].nbytes;
		nsubranges = 3;
	}
	return nsubranges;
}  // split_range

int host_array_alloc(host_array_t **p, void *hostptr, size_t nbytes, int idev) {
	*p = 0;
	host_array_t *new_host_array = (host_array_t*)smalloc(sizeof(host_array_t));
	//fprintf(stderr, "memory for host array allocated\n");
	if(!new_host_array)
		return GPUVM_ESALLOC;	
	memset(new_host_array, 0, sizeof(host_array_t));
	
	new_host_array->range.ptr = hostptr;
	new_host_array->range.nbytes = nbytes;
	new_host_array->links = (link_t**)smalloc(ndevs_g * sizeof(link_t));
	//fprintf(stderr, "memory for host array links allocated\n");
	if(!new_host_array->links) {
		sfree(new_host_array);
		return GPUVM_ESALLOC;
	}
	
	// generate subranges
	memrange_t subranges[MAX_SUBREGS];
	unsigned nsubregs = split_range(subranges, &new_host_array->range);
	unsigned isubreg;
	new_host_array->nsubregs = nsubregs;
	
	// allocate subregions
	int err;
	for(isubreg = 0; isubreg < nsubregs; isubreg++) {
		err = subreg_alloc(new_host_array->subregs + isubreg, 
											 subranges[isubreg].ptr, subranges[isubreg].nbytes, idev);
		//fprintf(stderr, "subregion allocated\n");
		if(err) {
			// free previously allocated subregions
			unsigned jsubreg;
			for(jsubreg = 0; jsubreg < isubreg; jsubreg++)
				subreg_free(new_host_array->subregs[jsubreg]);
			sfree(new_host_array->links);
			return err;
		}
		new_host_array->subregs[isubreg]->host_array = new_host_array;
	}  // for(isubreg)

	//fprintf(stderr, "subregions allocated\n");

	*p = new_host_array;
	return 0;
}  // host_array_alloc

void host_array_free(host_array_t *host_array) {
	if(!host_array)
		return;
	// free links
	//fprintf(stderr, "freeing links\n");
	unsigned ilink;
	for(ilink = 0; ilink < ndevs_g; ilink++)
		link_free(host_array->links[ilink]);
	sfree(host_array->links);
	// free subregions
	//fprintf(stderr, "freeing subregions\n");
	unsigned isubreg;
	for(isubreg = 0; isubreg < host_array->nsubregs; isubreg++)
		subreg_free(host_array->subregs[isubreg]);
	// free memory
	sfree(host_array);
	//fprintf(stderr, "freed host array\n");
	//fprintf(stderr, "host array deallocated\n");
}  // host_array_free

int host_array_find(host_array_t **p, void *hostptr, size_t nbytes) {
	*p = 0;
	// find region
	region_t *region = region_find_region(hostptr);
	if(!region)
		return 0;
	// find subregion
	subreg_t *subreg = region_find_subreg(region, hostptr);
	if(!subreg)
		return 0;
	*p = subreg->host_array;
	// check if array is the same
	if(nbytes == 0 || (*p)->range.ptr == hostptr && (*p)->range.nbytes == nbytes)
		return 0;
	else
		return 1;
} // host_array_find

int host_array_sync_to_device(host_array_t *host_array, unsigned idev, int flags) {
	if(!host_array->links[idev]) {
		fprintf(stderr, "host_array_sync_to_device: no link for array on device\n");
		return GPUVM_ENOLINK;
	}
	unsigned isubreg;
	int err;
	for(isubreg = 0; isubreg < host_array->nsubregs; isubreg++) {
		err = subreg_sync_to_device(host_array->subregs[isubreg], idev, flags);
		if(err)
			return err;
	}
	return 0;
}  // host_array_sync_to_device

int host_array_after_kernel(host_array_t *host_array, unsigned idev) {
	if(!host_array->links[idev]) {
		fprintf(stderr, "host_array_after_kernel: no link for array on device\n");
		return GPUVM_ENOLINK;
	}
	unsigned isubreg;
	int err;
	for(isubreg = 0; isubreg < host_array->nsubregs; isubreg++) {
		if(err = subreg_after_kernel(host_array->subregs[isubreg], idev))
			return err;
	}
	return 0;
}  // host_array_after_kernel

int host_array_remove_link(host_array_t *host_array, unsigned idev) {
	link_t **plink = &host_array->links[idev];
	link_free(*plink);
	*plink = 0;
	return 0;
}  // host_array_remove_link

int host_array_has_links(const host_array_t *host_array) {
	unsigned idev;
	for(idev = 0; idev < ndevs_g; idev++)
		if(host_array->links[idev])
			return 1;
	return 0;
}  // host_array_has_links
