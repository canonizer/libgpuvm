#ifndef GPUVM_CUDA_API_H_
#define GPUVM_CUDA_API_H_

#ifdef CUDA_ENABLED

/** @file cuda-api.h Calls for interacting with CUDA API */

/** initializes CUDA device API 
		@returns 0 if successful and a negative error code if not
 */
int cuda_devapi_init();

#endif

#endif
