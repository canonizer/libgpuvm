/** @file os-darwin.c 
		Darwin-specific implementations of "cross-OS" functions
*/

#ifdef __APPLE__

#include "gpuvm.h"
#include "semaph.h"
#include "util.h"

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/thread_info.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

thread_t self_thread() {
	return mach_thread_self();
}

int get_threads(thread_t **pthreads) {
	mach_port_t my_task = mach_task_self();
	thread_port_array_t thread_list;
	unsigned nthreads;
	int error;
	if(error = task_threads(my_task, &thread_list, &nthreads)) {
		fprintf(stderr, "get_threads: can\'t get a list of threads\n");
		return -1;
	}
	thread_t *threads = (thread_t*)malloc(nthreads * sizeof(thread_t));
	if(!threads) {
		fprintf(stderr, "get_threads: can\'t allocate memory for threads\n");
		vm_deallocate(my_task, (vm_address_t)thread_list, 
									sizeof(*thread_list) * nthreads);
		return -1;
	}
	memcpy(threads, thread_list, sizeof(thread_t) * nthreads);
	*pthreads = threads;
	return (int)nthreads;
}  // get_threads

void stop_other_threads(void) {

	// get current thread & task
	mach_port_t my_task = mach_task_self();
	thread_port_t my_thread = mach_thread_self();
	
	// get threads of current task
	thread_port_array_t thread_list;
	unsigned nthreads;
	kern_return_t error;

	error = task_threads(my_task, &thread_list, &nthreads);
	if(error)
		fprintf(stderr, "stop_other_threads: can\'t get list of threads\n");

	// stop all other threads
	//fprintf(stderr, "nthreads=%d\n", nthreads);
	unsigned ithread;
	for(ithread = 0; ithread < nthreads; ithread++) {
		//fprintf(stderr, "suspend self=%d, other=%d\n", 
		//					my_thread, thread_list[ithread]);
		if(thread_list[ithread] != my_thread) {
			//fprintf(stderr, "self=%d, suspending thread %d\n", 
			//				my_thread, thread_list[ithread]);
			error = thread_suspend(thread_list[ithread]);
			if(error) 
				fprintf(stderr, "stop_other_threads: can\'t suspend thread\n");
		}
		if(thread_list[ithread] != my_thread)
			mach_port_deallocate(my_task, thread_list[ithread]);
	}  // for(ithread)

	// free memory
	error = vm_deallocate(my_task, (vm_address_t)thread_list, 
								sizeof(*thread_list) * nthreads);
	if(error) 
		fprintf(stderr, "stop_other_threads: can\'t deallocate memory\n");
	// all other threads have been stopped
}  // stop_other_threads

void cont_other_threads(void) {
	// get current thread & task
	mach_port_t my_task = mach_task_self();
	thread_port_t my_thread = mach_thread_self();

	// get threads of current task
	thread_port_array_t thread_list;
	unsigned nthreads;
	kern_return_t error;
	error = task_threads(my_task, &thread_list, &nthreads);
	if(error)
		fprintf(stderr, "cont_other_threads: can\'t get list of threads\n");

	// resume all other threads
	//fprintf(stderr, "nthreads=%d\n", nthreads);
	unsigned ithread;
	for(ithread = 0; ithread < nthreads; ithread++) {
		//fprintf(stderr, "resume self=%d, other=%d\n", 
		//					my_thread, thread_list[ithread]);
		if(thread_list[ithread] != my_thread) {
			//fprintf(stderr, "self=%d, resuming thread %d\n", 
			//				my_thread, thread_list[ithread]);
			error = thread_resume(thread_list[ithread]);
			if(error) 
				fprintf(stderr, "cont_other_threads: can\'t suspend thread\n");
		}
		if(thread_list[ithread] != my_thread)
			mach_port_deallocate(my_task, thread_list[ithread]);
	}  // for(ithread)

	// free memory
	error = vm_deallocate(my_task, (vm_address_t)thread_list, 
												sizeof(*thread_list) * nthreads);
	if(error) 
		fprintf(stderr, "resume_other_threads: can\'t deallocate memory\n");
	// all other threads have been resumed
}

// semaphore utilities, from semaph.h
int semaph_init(semaph_t *sem, int value) {
	kern_return_t err = semaphore_create(mach_task_self(), sem, 0, value);
	if(err) {
		fprintf(stderr, "semaph_init: can\'t create a semaphore\n");
		return -1;
	}
	return 0;
}  // semaph_init

int semaph_post(semaph_t *sem) {
	kern_return_t err = semaphore_signal(*sem);
	if(err) {
		fprintf(stderr, "semaph_post: can\'t post on a semaphore\n");
		return -1;
	}
	return 0;
}  // semaph_post

int semaph_wait(semaph_t *sem) {
	kern_return_t err = semaphore_wait(*sem);
	if(err) {
		fprintf(stderr, "semaph_wait: can\'t wait on a semaphore\n");
		return -1;
	}
	return 0;
}  // semaph_wait

int semaph_destroy(semaph_t *sem) {
	kern_return_t err = semaphore_destroy(mach_task_self(), *sem);
	if(err) {
		fprintf(stderr, "semaph_destroy: can\'t destroy a semaphore\n");
		return -1;
	}
	return 0;
}  // semaph_destroy

#endif
