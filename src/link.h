#ifndef _GPUVM_LINK_H_
#define _GPUVM_LINK_H_

/** @file link.h 
		this file defines link, which "links" host array to a buffer on a specific device
*/

struct host_array_struct;

typedef struct link_struct {
	/** device buffer */
	void *buf;
	/** device for this link */
	unsigned idev;
	/** host array corresponding to the link */
	struct host_array_struct *host_array;
} link_t;

/** allocates a new link, and assigns it into the array
		@param p [out] - *p points to allocated link if successful and is 0 if not
		@param buf device buffer
		@param idev device number for which the link is created
		@param host_array host array associated with the link, or 0 if none
		@returns 0 if successful and negative error code if not
 */
int link_alloc(link_t **p, void *buf, unsigned idev, struct host_array_struct *host_array);

/** frees a previously allocated link 
		@param link the link to be freed
 */
void link_free(link_t *link);

#endif
