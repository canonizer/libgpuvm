/** @file cuda-api.c CUDA-based API for host-device interaction */

#ifdef CUDA_ENABLED

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <stdio.h>

#include "devapi.h"
#include "gpuvm.h"
#include "util.h"

/** a CUDA function for device-to-host copy
		@param idev GPUVM device number
		@param tgt target pointer, that is, host pointer
		@param src source pointer, that is, device pointer
		@param nbytes how many bytes to copy
		@param devoff offset in device buffer
		@returns 0 if successful and a negative error code if not
 */
static int cuda_memcpy_d2h
(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);

/** a CUDA function for host-to-device copy
		@param idev GPUVM device number
		@param tgt target pointer, that is, device pointer
		@param src source pointer, that is, host pointer
		@param nbytes how many bytes to copy
		@param devoff offset in device buffer
		@returns 0 if successful and a negative error code if not
 */
static int cuda_memcpy_h2d
(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);

int cuda_devapi_init() {
		// fill in devapi_g structure
	devapi_g = (devapi_t*)smalloc(sizeof(devapi_t));
	if(!devapi_g)		
		return GPUVM_ESALLOC;
	devapi_g->memcpy_d2h = cuda_memcpy_d2h;
	devapi_g->memcpy_h2d = cuda_memcpy_h2d;
	return 0;
}  // cuda_devapi_init()

// TODO: avoid always getting/setting CUDA device, find another way
static int cuda_memcpy_d2h
(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff) {
	int prev_device;
	cudaGetDevice(&prev_device);
	cudaSetDevice((int)idev);
	
	cudaError_t err = cudaMemcpy
		(tgt, (char*)src + devoff, nbytes, cudaMemcpyDeviceToHost);
	if(!err)
		err = cudaDeviceSynchronize();

	cudaSetDevice(prev_device);

	if(err != cudaSuccess) {
		fprintf(stderr, "cuda_memcpy_d2h: can\'t copy data\n");
		return -1;
	}
	return 0;
}  // cuda_memcpy_d2h

static int cuda_memcpy_h2d
(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff) {
	int prev_device;
	cudaGetDevice(&prev_device);
	cudaSetDevice((int)idev);
	
	cudaError_t err = cudaMemcpy
		((char*)tgt + devoff, src, nbytes, cudaMemcpyHostToDevice);
	if(!err)
		err = cudaDeviceSynchronize();

	cudaSetDevice(prev_device);

	if(err != cudaSuccess) {
		fprintf(stderr, "cuda_memcpy_h2d: can\'t copy data\n");
		return -1;
	}
	return 0;
}  // cuda_memcpy_h2d

#endif
