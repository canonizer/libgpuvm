#ifndef GPUVM_OPENCL_API_H_
#define GPUVM_OPENCL_API_H_

#ifdef OPENCL_ENABLED

/** @file opencl-api.h 
		functions used by GPUVM to interact with OpenCL runtime
 */

#include <stddef.h>

/** initializes OpenCL device API 
		@returns 0 if successful and a negative error code if not
 */
int ocl_devapi_init();

#endif
#endif
