#ifndef GPUVM_STAT_H
#define GPUVM_STAT_H

/** @file stat.h interface to statistics collection of GPUVM */

extern volatile int stat_enabled_g;

extern volatile double copy_time_g;

/** 
		initializes statisitcs collection on GPUVM
		@param flags statistics collection flags. Currently only GPUVM_STAT is supported,
		which enables statistics collection
		@returns 0 if successful and a negative error code if not
 */
int stat_init(int flags);

/** 
		atomically accumulates value into a double counter
		@param parameter the parameter into which to accumulate. Currently, GPUVM_COPY_TIME
		@param value the value which to add
		@returns 0 if successful and a negative error code if not
 */
int stat_acc_double(int parameter, double value);

#endif
