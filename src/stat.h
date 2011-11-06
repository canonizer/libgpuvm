#ifndef GPUVM_STAT_H
#define GPUVM_STAT_H

/** @file stat.h interface to statistics collection and control variables 
		of GPUVM */

/** enumeration for libgpuvm control flags */
typedef enum {
	/** indicates whether statistics collection is enabled */
	CTL_STAT_ENABLED = 0x1,
	/** indicates whether blocking of certain signals inside writer lock is
			enabled */
	CTL_WRITER_SIG_BLOCK = 0x2,
	/** indicates whether the data must be sync'ed back on unlinking */
	CTL_UNLINK_SYNC_BACK = 0x4
} flags_ctl_t;

/** control flags */
//extern volatile flags_ctl_t flags_ctl_g;

/** enumeration used for control flags */
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

/** gets whether statistics collection is enabled 
		@returns non-zero if statistics collection is enabled and 0 if not
 */
int stat_enabled(void);

/** gets whether sync'ing back on unlink is enabled 
		@returns non-zero if sync'ing back on unlink is enabled and 0 if not
 */
int stat_unlink_sync_back(void);

/** gets whether writer must block signals 
		@returns non-zero if writer must block/unblock signals and 0 if it must not
 */
int stat_writer_sig_block(void);

#endif
