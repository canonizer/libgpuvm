#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpuvm.h"
#include "region.h"
#include "subreg.h"
#include "tsem.h"
#include "util.h"
#include "wthreads.h"

#ifdef __APPLE__
#define SIG_PROT SIGBUS
#else
#define SIG_PROT SIGSEGV
#endif

/** the old handler */
void (*old_handler_g)(int, siginfo_t*, void*);

void sigprot_handler(int signum, siginfo_t *siginfo, void *ucontext);
void sigsusp_handler(int signum, siginfo_t *siginfo, void *ucontext);

/** a semaphore used to block threads */
//sem_t thread_block_sem;

/** set up the new handler */
int handler_init() {
	struct sigaction action, old_action;

	// new signal structure
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigfillset(&action.sa_mask);
	sigdelset(&action.sa_mask, SIGABRT);
	sigdelset(&action.sa_mask, SIGCONT);
	//sigdelset(&action.sa_mask, SIG_PROT);
	action.sa_sigaction = sigprot_handler;
	
	// set SIGSEGV handler
	if(sigaction(SIG_PROT, &action, &old_action)) {
		fprintf(stderr, "hander_init: can\'t set SIG_PROT handler\n");
		return GPUVM_ERROR;
	}
	
	// remember old handler
	old_handler_g = old_action.sa_sigaction;

	// set suspension signal handler - for non-Darwin only
	#ifndef __APPLE__
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigfillset(&action.sa_mask);
	sigdelset(&action.sa_mask, SIGCONT);
	action.sa_sigaction = sigsusp_handler;
	if(sigaction(SIG_SUSP, &action, 0)) {
		fprintf(stderr, "handler_init: can\'t set SIG_SUSP handler\n");
		return GPUVM_ERROR;
	}
	#endif

	return 0;
}  // handler_init

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
void sigprot_handler(int signum, siginfo_t *siginfo, void *ucontext) {

	// TODO: pass info about the thread which caused the exception to the main 
	//int tid = self_thread();
	//fprintf(stderr, "thread %d: in SIGSEGV handler\n", tid);
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

	// acquire global reader lock
	lock_reader();

	// check if we handle the SIG_PROT address
	region_t *region = region_find_region(ptr);
	if(!region) {
		// we don't handle the address
		sync_unlock();
		call_old_handler(signum, siginfo, ucontext);
		return;
	}

	// queue the region & wait for removal of protection
	// note that we don't need to wait further:
	// - OpenCL and GPUVM threads ("immune") mustn't wait, as they do not use
	// protected arrays, and stopping them may cause deadlocks
	// - application threads needn't wait as they're stopped anyway
	wthreads_put_region(region);
	region_wait_unprotect(region);

	// it is safe to continue now
	sync_unlock();

	//fprintf(stderr, "thread %d: leaving SIGSEGV handler\n", tid);
}  // sigsegv_handler()

#ifndef __APPLE__
void sigsusp_handler(int signum, siginfo_t *siginfo, void *ucontext) {
	int tid = self_thread();
	tsem_t *tsem = tsem_get(tid);
	tsem_wait(tsem);
	// test implementation - just sleep it out
	// sleep(1);
	//fprintf(stderr, "thread %d: in SIG_SUSP handler\n", tid);
	//self_block_wait();
	//fprintf(stderr, "thread %d: leaving SIG_SUSP handler\n", tid);
}
#endif
