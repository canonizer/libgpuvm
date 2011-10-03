/** @file os-darwin.c 
		Darwin-specific implementations of "cross-OS" functions
*/

#ifdef __APPLE__

#include "gpuvm.h"
#include "util.h"

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/task_info.h>
#include <mach/thread_info.h>

#include <stdio.h>
#include <unistd.h>

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
	//mach_port_deallocate(my_task, my_thread);
	//mach_port_deallocate(my_task, my_task);
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
	//mach_port_deallocate(my_task, my_thread);
	//mach_port_deallocate(my_task, my_task);

	// all other threads have been resumed
}

#endif
