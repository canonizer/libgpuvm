#ifndef GPUVM_DEVAPI_H_
#define GPUVM_DEVAPI_H_

/** @file devapi.h
		API for interaction with device, either CUDA or OpenCL
*/

/** describes an abstract API for interaction with device, such as CUDA or
		OpenCL. As everywhere in libgpuvm, functions accept arguments, first of
		which is the device number, and then go other arguments. All functions
		return error code */
typedef struct devapi_struct {
	
	/** copies data synchronously from host to device; also updates device-related
			time counters if provided by device
			@param idev GPUVM device number
			@param tgt target pointer, that is, device pointer
			@param src source pointer, that is, host pointer
			@param nbytes how many bytes to copy
			@param devoff offset in device buffer
			@returns 0 if successful and a negative error code if not
	 */
	int (*memcpy_h2d)(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);

	/** copies data synchronously from device to host; also updates device-related
			time counters if provided by device
			@param idev GPUVM device number
			@param tgt target pointer, that is, host pointer
			@param src source pointer, that is, device pointer
			@param nbytes how many bytes to copy
			@param devoff offset in device buffer
			@returns 0 if successful and a negative error code if not
	 */
	int (*memcpy_d2h)(unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);

} devapi_t;

/** global devapi variable pointer */
extern devapi_t *devapi_g;

/** initializes device API */
int devapi_init(int flags);

/** a wrapper function for host-to-device copy which also collects
		device-independent information, such as timing info returned by
		OS. Arguments are the same as for devapi->memcpy_h2d
		@param devapi API used to interact with device
		@param idev GPUVM device number
		@param tgt target pointer, that is, device pointer
		@param src source pointer, that is, host pointer
		@param nbytes how many bytes to copy
		@param devoff offset in device buffer
		@returns 0 if successful and a negative error code if not
 */
int memcpy_h2d
(devapi_t *devapi, unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);

/** a wrapper function for device-to-host copy which also collects
		device-independent information, such as timing info returned by
		OS. Arguments are the same as for devapi->memcpy_h2d
		@param devapi API used to interact with device
		@param idev GPUVM device number
		@param tgt target pointer, that is, host pointer
		@param src source pointer, that is, device pointer
		@param nbytes how many bytes to copy
		@param devoff offset in device buffer
		@returns 0 if successful and a negative error code if not
 */
int memcpy_d2h
(devapi_t *devapi, unsigned idev, void *tgt, void *src, size_t nbytes, size_t devoff);
#endif
