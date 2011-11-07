/** @file gpuvm.h public interface to General-Purpose Userland Virtual Memory (GPUVM) library */

#ifndef _GPUVM_H_
#define _GPUVM_H_

#include <stddef.h>

/** page size used in the system */
#define GPUVM_PAGE_SIZE 4096

/** indicates that all devices must be unlinked */
#define GPUVM_ALL_DEVICES ~0

/** flags specifying device type, data placement, array usage etc. Constants of this type must be used directly  */
enum {
	/** no flags */
  GPUVM_NONE = 0,
	/** OpenCL device */
	GPUVM_OPENCL = 0x1,
	/** CUDA device */
	GPUVM_CUDA = 0x2,
	/** data in the array being linked reside on host */
	GPUVM_ON_HOST = 0x4,
	/** data in the array being linked reside on device */
	GPUVM_ON_DEVICE = 0x8,
	/** data are to be used in kernel only for reading */
	GPUVM_READ_ONLY = 0x10,
	/** data are to be used in kernel only for writing */
	GPUVM_WRITE_ONLY = 0x20,
	/** data are to be used in kernel both for reading and writing */
	GPUVM_READ_WRITE = 0x30,
	/** enable collection of statistics on devices */
	GPUVM_STAT = 0x40,
	/** record list of threads before runtime (OpenCL) initialization */
	GPUVM_THREADS_BEFORE_INIT = 0x80,
	/** record list of threads after runtime (OpenCL) initializaiton, and subtract from it
			the list of threads before initialization, to get the final list of threads which
			must not be stopped */
	GPUVM_THREADS_AFTER_INIT = 0x100,
	/** enable blocking of certain signals when holding global writer lock; 
	 this is required to work correctly with mono GC */
	GPUVM_WRITER_SIG_BLOCK = 0x200,
	/** do not sync data back to host prior to unlinking */
	GPUVM_UNLINK_NO_SYNC_BACK = 0x400
};

/** constants specifying different types of errors */
enum {
	/** general error code, if nothing more specific can be provided */
	GPUVM_ERROR = -1,
	/** can't allocate separate special memory, or resources for that memory
			if inside gpuvm_init()*/
	GPUVM_ESALLOC = -2,
	/** one of argument is null and it is not allowed */
	GPUVM_ENULL = -3,
	/** one of arguments is out of range or is invalid */
	GPUVM_EARG = -4,
  /** the call is performed twice while allowed only once, e.g. initialization or
			recording of initial number of threads */
	GPUVM_ETWICE = -5,
	/** the range is already registered with GPUVM */
	GPUVM_ERANGE = -6,
	/** the link for specified device already exists */
	GPUVM_ELINK = -7,
	/** the specified host pointer does not point within a valid host array */
	GPUVM_EHOSTPTR = -8,
	/** call with underlying implementation failed, likely because of insufficient
			memory. If memory is managed with garbage collection, try it and call 
			GPUVM again */
	GPUVM_EDEVALLOC = -9,
	/** call setting/removing memory protection fails for some reason */
	GPUVM_EPROT = -10,
	/** the array has no link for the specified device */
	GPUVM_ENOLINK = -11,
	/** the call is not allowed in this state, or other actions are required */
	GPUVM_ESTATE = -12
};

/** possible values, including counters and parameters, which can be obtained using
		gpuvm_stat() call. The types for the respective counters are specified as well
*/
enum {
	/** whether GPUVM statistics collection is enabled, int (nonzero if enabled and zero if not) */
	GPUVM_STAT_ENABLED = 1,
	/** number of devices, unsigned */
	GPUVM_STAT_NDEVS = 2,
	/** total copying time (measured by OpenCL) in seconds, double */
	GPUVM_STAT_COPY_TIME = 3
};

/** 
		must be called before and after initialization of OpenCL runtime. The threads which
		belong to OpenCL runtime will be recorded, and not touched during thread 
		stopping/starting
		@param flags must be ::GPUVM_THREADS_BEFORE_INIT if called before OpenCL runtime
		initialization, and ::GPUVM_THREADS_AFTER_INIT if called after OpenCL runtime
		initialization
		@return 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_pre_init(int flags);

/** 
		gets whether GPUVM library exists in the system. This is the only method which can be
		called prior to gpuvm_init()
		@returns nonzero if the library exists
		@remarks if the library doesn't exist or can't be found on the target system,
		typically an error or an exception will be generated, which can be caught and
		handled. This method is primarily intended to allow NUDA to detect GPUVM library
		before all devices are initialized. If the library doesn't exist, calling this method
		will result in a DllNotFoundException (as it is in mono)
 */
__attribute__((visibility("default")))
int gpuvm_library_exists();

/** 
		initializes GPUVM library, must be called once per process 
		@param ndevs number of devices to be used in the library
		@param devs devices to be used in the library. For OpenCL, each pointer must specify a
		device queue. 
		@param flags indicate device type and possibly usage strategy. Currently must include
		::GPUVM_OPENCL, and a combination of optional ::GPUVM_STAT, ::GPUVM_WRITER_SIG_BLOCK
		and ::GPUVM_UNLINK_NO_SYNC_BACK.
		Note that if ::GPUVM_STAT is specified for OpenCL devices, the underlying
		OpenCL queue must have profiling enabled, or OpenCL-related errors will occur during
		further operation
		@return 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_init(unsigned ndevs, void **devs, int flags);

/** 
		links a host-side array and device-side buffer, so they are managed together by GPUVM
		library, until the data is unlinked via gpuvm_unlink()
		@param hostptr host pointer indicating the start of the host-side array being
		"linked". Must not be null
		@param nbytes size of host data block in bytes. Zero-sized data blocks are not
		allowed, so it must be > 0
		@param idev device number with which a link is created. Only one link may be created
		for a single device
		@param devbuf device-side buffer being linked to host-side array. May be NULL if
		nbytes == 0
		@param flags indicating device type and initial data placement. Currently,
		must include obligatory ::GPUVM_OPENCL, and one of ::GPUVM_ON_HOST or
		::GPUVM_ON_DEVICE, to indicate initial data placement (on host or on device,
		respectively). Note that if data is said to be placed on device, the array
		may not be previosly registered with GPUVM, or an error will result
		@returns 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_link(void *hostptr, size_t nbytes, unsigned idev, void *devbuf, int flags);

/** 
		unlinks an array which was previously linked, on a single device. If the array is
		not linked on the specified device, nothing is done and 0 is returned. If the
		specified device is the last on which the host array is linked, then the host array is
		removed from monitoring by GPUVM
		@param hostptr a pointer previously linked with gpuvm_link
		@param idev the device on which to unlink the buffer
		@returns 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_unlink(void *hostptr, unsigned idev);

/** 
		indicates that the device array corresponding to host array is about to be used in a
		kernel, so make its state on device actual
		@param hostptr a pointer previously linked to device buffer which is about to be used
		in a kernel
		@param idev number of device on which a kernel is about to be launched
		@param flags flags indicating possible buffer usage on device. Currently, must be
		::GPUVM_READ_WRITE 
		@returns 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_kernel_begin(void *hostptr, unsigned idev, int flags);

/** 
		indicates that using device array in the kernel is finished, and appropriate
		protection may be necessary to be set on host
		@param hostptr a pointer previously linked to device buffer and used in a kernel
		@param idev device on which a kernel has recently finished
		@returns 0 if successful and error code if not
 */
__attribute__((visibility("default")))
int gpuvm_kernel_end(void *hostptr, unsigned idev);

/** 
		gets the value of a certain GPUVM counter or parameter
		@param parameter the parameter; currently available parameters are ::GPUVM_STAT_NDEVS,
		::GPUVM_STAT_ENABLED and ::GPUVM_STAT_COPY_TIME
		@param value pointer to the returned value. The type of the value pointed to must be
		the same as the type of the requested paramter
 */
__attribute__((visibility("default")))
int gpuvm_stat(int parameter, void *value);
// FINISHED HERE - provide implementation

#endif
