#ifndef _GPUVM_HANDER_H_
#define _GPUVM_HANDER_H_

/** @file handler.h public interface for setting SIGSEGV handler during initialization */

/** sets up SIGSEGV handler
		@returns 0 if successful and a negative error code if not
 */
int hander_init();

#endif
