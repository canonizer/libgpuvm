#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpuvm.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

//#define SIGCODE SIGSEGV

// TODO: also handle it in multithreaded case
#define MAX_REGION_STACK_SIZE 64

/** the old handler */
void (*old_handler_g)(int, siginfo_t*, void*);

/** whether SIGSEGV occured from within SIGSEGV handler; used to detect multiple SIGSEGV 
 */
volatile int in_handler_g = 0;

/** region stack */
region_t *region_stack_g[MAX_REGION_STACK_SIZE];

/** pointer to region stack*/
volatile unsigned region_stack_ptr_g = 0;

void sigsegv_handler(int signum, siginfo_t *siginfo, void *ucontext);

/** set up the new handler */
int handler_init() {
	struct sigaction action, old_action;

	// new signal structure
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigfillset(&action.sa_mask);
	sigdelset(&action.sa_mask, SIGABRT);
	sigdelset(&action.sa_mask, SIGSEGV);
	action.sa_sigaction = sigsegv_handler;
	
	// set SIGSEGV handler
	if(sigaction(SIGSEGV, &action, &old_action)) {
		fprintf(stderr, "hander_init: can\'t set SIGSEGV handler\n");
		return GPUVM_ERROR;
	}
	
	// remember old handler
	old_handler_g = old_action.sa_sigaction;
	return 0;
}

/** the function to call old handler */
void call_old_handler(int signum, siginfo_t *siginfo, void *ucontext) {
	// TODO: implement calling old signal handler
	if((void*)old_handler_g == (void*)SIG_IGN) 
		return;
	if((void*)old_handler_g == (void*)SIG_DFL) {
		fprintf(stderr, "segmentation fault\n");
		abort();
	}
	old_handler_g(signum, siginfo, ucontext);
}  // call_old_handler()

/** signal handler to be fed into sigaction() 
		no reaction to error codes inside signal handler as there's no return value
		if there is any error, then it's too late to handle it anyway
 */
void sigsegv_handler(int signum, siginfo_t *siginfo, void *ucontext) {

	// cut off NULL addresses and signals not caused by mprotect
	void *ptr = siginfo->si_addr;
	
	if(!ptr) {
		//fprintf(stderr, "sigsegv_handler: pointer is NULL \n");
		call_old_handler(signum, siginfo, ucontext);
		return;
	}
	if(siginfo->si_code != SEGV_ACCERR) {
		//fprintf(stderr, "sigsegv_handler: not an mprotect error, ptr = %tx\n", ptr);
		call_old_handler(signum, siginfo, ucontext);
		return;
	}
	// check if this is a second segmentation fault
	int in_second_fault = 0;
	if(in_handler_g) {
		in_second_fault = 1;
		//fprintf(stderr, "sigsegv_handler: second segmentation fault, ptr = %tx\n", ptr);
	}

	// acquire global reader lock
	lock_reader();
	in_handler_g = 1;
	//fprintf(stderr, "searching in region tree\n");
	// check if we handle the SIGSEGV address
	region_t *region = region_find_region(ptr);
	if(!region) {
		if(!in_second_fault)
			in_handler_g = 0;
		// we don't handle the address
		sync_unlock();
		call_old_handler(signum, siginfo, ucontext);
		return;
	}

	//fprintf(stderr, "region found, removing protection\n");
	// remove region memory protection
	region_lock(region);
	region_unprotect(region);
	region_unlock(region);

	if(in_second_fault) {
		if(region_stack_ptr_g >= MAX_REGION_STACK_SIZE) {
			fprintf(stderr, "sigsegv_hander: region stack size exceeded, aborting\n");
			abort();
		}
		// push region into the stack
		//in_handler_g = 0;
		region_stack_g[region_stack_ptr_g++] = region;
	}

	//fprintf(stderr, "copying data back\n");
	// sync all subregions of current region to host
	subreg_list_t *list;
	if(!in_second_fault) {
		for(list = region->subreg_list; list; list = list->next)
			subreg_sync_to_host(list->subreg);
	}

	/*region_lock(region);
	region_unprotect(region);
	region_unlock(region);*/

	// handle additional regions from the stack
	if(!in_second_fault) {
		region_t *stack_region;
		while(region_stack_ptr_g > 0) {
			stack_region = region_stack_g[--region_stack_ptr_g];
			for(list = stack_region->subreg_list; list; list = list->next)
				subreg_sync_to_host(list->subreg);				
		}  // end of while()
		in_handler_g = 0;
	}

	// release global reader lock
	sync_unlock();
}  // sigsegv_handler()
