#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "gpuvm.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

#ifdef __APPLE__
#define SIG_PROT SIGBUS
#else
#define SIG_PROT SIGSEGV
#endif

#define MAX_REGION_STACK_SIZE 64

// maximum path length for directories in /proc
#define MAX_PROC_PATH 127

#ifndef __APPLE__
/** wrapper for gettid syscall */
pid_t gettid(void) {
	return (pid_t)syscall(SYS_gettid);
}

/** wrapper for tgkill syscall */
int tgkill(int tgid, int tid, int sig) {
	return syscall(SYS_tgkill, tgid, tid, sig);
}

#endif

/** the old handler */
void (*old_handler_g)(int, siginfo_t*, void*);

/** whether SIGSEGV occured from within SIGSEGV handler; used to detect multiple SIGSEGV 
 */
volatile int in_handler_g = 0;

/** region stack */
region_t *region_stack_g[MAX_REGION_STACK_SIZE];

/** pointer to region stack*/
volatile unsigned region_stack_ptr_g = 0;

void sigprot_handler(int signum, siginfo_t *siginfo, void *ucontext);

/** set up the new handler */
int handler_init() {
	struct sigaction action, old_action;

	// new signal structure
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigfillset(&action.sa_mask);
	sigdelset(&action.sa_mask, SIGABRT);
	sigdelset(&action.sa_mask, SIG_PROT);
	action.sa_sigaction = sigprot_handler;
	
	// set SIGSEGV handler
	if(sigaction(SIG_PROT, &action, &old_action)) {
		fprintf(stderr, "hander_init: can\'t set SIG_PROT handler\n");
		return GPUVM_ERROR;
	}
	
	// remember old handler
	old_handler_g = old_action.sa_sigaction;
	return 0;
}

/** 
		stops all threads except for the caller thread. Used to prevent false sharing errors
		between removing of memory protection and actualizing host buffer state. No errors are
		handled inside this function and no errors are returned, as it is always late to
		handle errors inside signal hander. Supported only on Linux or other systems with
		/proc file system. On Darwin it is a no-op, therefore, only single-threaded
		applications are supported.
 */
static void stop_other_threads(void) {
#ifndef __APPLE__
	// get current thread id and process id's
	pid_t my_tid = gettid();
	pid_t my_pid = getpid();

	// directory of threads for the current process
	char task_dir_path[MAX_PROC_PATH + 1];
	//char thread_stat_path[MAX_PROC_PATH + 1];
	memset(task_dir_path, 0, MAX_PROC_PATH + 1);
	//memset(thread_stat_path, 0, MAX_PROC_PATH + 1);
	snprintf(task_dir_path, MAX_PROC_PATH + 1, "/proc/%d/task", (int)my_pid);
	int stop_every_thread = 1;
	int running_thread_found = 1;
	while(running_thread_found) {
		running_thread_found = 0;
		DIR *task_dir = opendir(task_dir_path);
		struct dirent *thread_dirent;
		while(thread_dirent = readdir(task_dir)) {
			int other_tid;
			if(!strcmp(thread_dirent->d_name, ".") || !strcmp(thread_dirent->d_name, ".."))
				continue;
			if(sscanf(thread_dirent->d_name, "%d", &other_tid))	{
				if((pid_t)other_tid == my_tid)
					continue;
				int stop_this_thread = stop_every_thread;
				if(!stop_every_thread) {
					// check status of this thread
					// TODO: do the actual check - currently simply assume it is not needed
					//snprintf(thread_stat_path, MAX_PROC_PATH + 1, "/proc/%d/task/%d/stat", 
					//				 (int)my_pid, other_tid);
					stop_this_thread = 0;
				}
				if(stop_this_thread)
					tgkill(-1, (pid_t)other_tid, SIGSTOP);
			} else {
				fprintf(stderr, "stop_other_threads: %s: non-numeric subdir of thread dir "
								"found\n", thread_dirent->d_name);
			}  // if(other_tid)
		}  // while(thread_dirent)
		if(stop_every_thread) {
			stop_every_thread = 0;
			running_thread_found = 1;
		}
	}  // end of while()
	// every thread except for the caller is stopped now
#endif
}  // stop_other_threads()

/** 
		resumes other threads, except for the caller, after they have been stopped. No errors
		are handled inside this function and no errors are returned, as it is always late to
		handle errors inside signal hander. Supported only on Linux or other systems with
		/proc file system. On Darwin it is a no-op, therefore, only single-threaded
		applications are supported.
 */

static void cont_other_threads(void) {
#ifndef __APPLE__
	// get current thread id and process id's
	pid_t my_tid = gettid();
	pid_t my_pid = getpid();

	// directory of threads for the current process
	char task_dir_path[MAX_PROC_PATH + 1];
	memset(task_dir_path, 0, MAX_PROC_PATH + 1);
	snprintf(task_dir_path, MAX_PROC_PATH + 1, "/proc/%d/task", (int)my_pid);
	DIR *task_dir = opendir(task_dir_path);
	struct dirent *thread_dirent;
	while(thread_dirent = readdir(task_dir)) {
		if(!strcmp(thread_dirent->d_name, ".") || !strcmp(thread_dirent->d_name, ".."))
			continue;
		int other_tid;
		if(sscanf(thread_dirent->d_name, "%d", &other_tid))	{
			if((pid_t)other_tid == my_tid)
				continue;
			tgkill(-1, (pid_t)other_tid, SIGCONT);
		} else {
			fprintf(stderr, "cont_other_threads: %s: non-numeric subdir of thread dir "
							"found\n", thread_dirent->d_name);
		}  // if(other_tid)
	}  // while(thread_dirent)
	// every thread stopped in stop_other_threads() has been resumed
#endif
}  // cont_other_threads

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

	// check if this is a second segmentation fault
	int in_second_fault = 0;
	if(in_handler_g) {
		in_second_fault = 1;
		//fprintf(stderr, "sigsegv_handler: second segmentation fault, ptr = %tx\n", ptr);
	}

	in_handler_g = 1;

	// check if we handle the SIG_PROT address
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
			fprintf(stderr, "sigprot_hander: region stack size exceeded, aborting\n");
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
		stop_other_threads();
		for(list = region->subreg_list; list; list = list->next)
			subreg_sync_to_host(list->subreg);

		// handle additional regions from the stack
		region_t *stack_region;
		while(region_stack_ptr_g > 0) {
			stack_region = region_stack_g[--region_stack_ptr_g];
			for(list = stack_region->subreg_list; list; list = list->next)
				subreg_sync_to_host(list->subreg);				
		}  // end of while()
		in_handler_g = 0;
		cont_other_threads();
	}  // if(!in_second_fault)

	// release global reader lock
	sync_unlock();
}  // sigsegv_handler()
