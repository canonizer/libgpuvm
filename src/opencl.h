#ifndef _GPUVM_OPENCL_H_
#define _GPUVM_OPENCL_H_

/** @file opencl.h 
		functions used by GPUVM to interact with OpenCL runtime
 */

#include <stddef.h>

struct link_struct;

/** copies data from the device buffer to host buffer. This is a blocking copy
		@param hostptr starting address of the host buffer
		@param nbytes number of bytes to copy
		@param idev number of the device on which the buffer resides
		@param devbuf the device buffer
		@param offset offset in device buffer at which to start copying
		@returns 0 if successful and a negative error code if not
 */
int ocl_sync_to_host(void *hostptr, size_t nbytes, unsigned idev, void *devbuf, 
										 size_t offset);

/** copies data from host buffer to device buffer. This is a blocking copy 
		@param hostptr starting address of the host buffer
		@param nbytes number of bytes to copy
		@param idev number of the device on which the buffer resides
		@param devbuf the device buffer
		@param offset offset in device buffer at which to start copying
		@returns 0 if successful and a negative error code if not
 */
int ocl_sync_to_device(const void *hostptr, size_t nbytes, unsigned idev, void *devbuf,
											 size_t offset);

#endif
