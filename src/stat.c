/** @file implementation of statistics collection of GPUVM */

#include <pthread.h>
#include <stdio.h>

#include "gpuvm.h"
#include "stat.h"

extern unsigned ndevs_g;

/** GPUVM control flags */
flags_ctl_t flags_ctl_g = 0;

/** mutex to update copy time */
pthread_mutex_t copy_time_mutex_g;

/** total time (in seconds) spent in copying data */
volatile double copy_time_g = 0.0;

int stat_init(int flags) {
	if(pthread_mutex_init(&copy_time_mutex_g, 0)) {
		fprintf(stderr, "init_stat: can\'t initialize mutex");
		return GPUVM_ERROR;
	}
	if(flags & GPUVM_STAT)
		flags_ctl_g |= CTL_STAT_ENABLED;
	if(flags & GPUVM_WRITER_SIG_BLOCK)
		flags_ctl_g |= CTL_WRITER_SIG_BLOCK;
	if(!(flags & GPUVM_UNLINK_NO_SYNC_BACK))
		flags_ctl_g |= CTL_UNLINK_SYNC_BACK;
	return 0;
}  // init_stat

int gpuvm_stat(int parameter, void *value) {
	if(!value) {
		fprintf(stderr, "gpuvm_stat: pointer to value is NULL\n");
		return GPUVM_ENULL;
	}
	switch(parameter) {
	case GPUVM_STAT_ENABLED:
		*(int*)value = CTL_STAT_ENABLED;
		return 0;
	case GPUVM_STAT_NDEVS:
		*(unsigned*)value = ndevs_g;
		return 0;
	case GPUVM_STAT_COPY_TIME:
		*(double*)value = copy_time_g;
		return 0;
	default:
		fprintf(stderr, "gpuvm_stat: parameter value is invalid\n");
		return GPUVM_EARG;
	}  // switch(parameter)
}  // gpuvm_stat

int stat_enabled(void) { return flags_ctl_g & CTL_STAT_ENABLED; }

int stat_writer_sig_block(void) {	return flags_ctl_g & CTL_WRITER_SIG_BLOCK; }

int stat_unlink_sync_back(void) {return flags_ctl_g & CTL_UNLINK_SYNC_BACK; }

int stat_acc_double(int parameter, double value) {
	// currently, only GPUVM_COPY_TIME
	if(pthread_mutex_lock(&copy_time_mutex_g)) {
		fprintf(stderr, "stat_acc_double: can\'t lock mutex\n");
		return GPUVM_ERROR;
	}
	copy_time_g += value;
	if(pthread_mutex_unlock(&copy_time_mutex_g)) {
		fprintf(stderr, "stat_acc_double: can\'t unlock mutex\n");
		return GPUVM_ERROR;
	}
	return 0;
}  // stat_acc_double
