#include "gpuvm.h"
#include "host-array.h"
#include "link.h"
#include "util.h"

int link_alloc(link_t **p, void *buf, unsigned idev, host_array_t *host_array) {
	if(!(*p = (link_t*)smalloc(sizeof(link_t)))) 
		return GPUVM_ESALLOC;
	link_t *link = *p;
	link->buf = buf;
	link->idev = idev;
	link->host_array = host_array;
	host_array->links[idev] = link;
	return 0;
}

void link_free(link_t *link) {
	if(!link)
		return;
	sfree(link);
}
