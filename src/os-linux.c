/** @file os-linux.c
		Linux-specific implementations of "cross-OS" functions
 */

#ifndef __APPLE__

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpuvm.h"
#include "util.h"

// maximum path length for directories in /proc
#define MAX_PROC_PATH 127

// buffer size for some internal needs
#define BUFFER_SIZE 64

/** array of threads stopped by libgpuvm, linux only */
pid_t stopped_threads_g[MAX_NTHREADS];

/** number of threads stopped by libgpuvm, linux only */
unsigned nstopped_threads_g = 0;

/** wrapper for gettid syscall */
static pid_t gettid(void) {
	return (pid_t)syscall(SYS_gettid);
}

/** wrapper for tgkill syscall */
static int tgkill(int tgid, int tid, int sig) {
	return syscall(SYS_tgkill, tgid, tid, sig);
}

int get_threads(thread_t **pthreads) {
	char task_dir_path[] = "/proc/self/task";
	DIR *task_dir = opendir(task_dir_path);
	if(!task_dir) {
		fprintf(stderr, "get_threads: can\'t open %s\n", task_dir_path);
		return -1;
	}

	// count threads
	unsigned nthreads = 0;
	while(readdir(task_dir)) nthreads++;
	// subtract . and ..
	nthreads -= 2;
	
	// fill in thread numbers
	rewinddir(task_dir);
	thread_t *threads = (thread_t*)malloc(sizeof(thread_t) * nthreads);
	if(!threads) {
		fprintf(stderr, "get_threads: can\'t allocate memory for thread ids\n");
		closedir(task_dir);
		return -1;
	}
	struct dirent *thread_dir;
	unsigned ithread = 0;
	while(thread_dir = readdir(task_dir)) {
		if(!strcmp(thread_dir->d_name, ".") || !strcmp(thread_dir->d_name, "..")) 
			continue;
		int thread_id;
		if(sscanf(thread_dir->d_name, "%d", &thread_id) < 1) {
			fprintf(stderr, "get_threads: invalid thread id %s\n",
							thread_dir->d_name);
			free(threads);
			closedir(task_dir);
			return -1;
		}
		threads[ithread++] = (thread_t)thread_id;
	}  // while(readdir)
	closedir(task_dir);
	*pthreads = threads;
	return nthreads;
}  // get_threads

/** checks whether the thread must be stopped; the thread is identified by its tid;
		linux only 
		@param tid thread id of the thread to be checked
		@returns non-zero if the thread must be stopped and zero if not
		@remarks a zombie thread must not be stopped, and will never change to
		stopped state; a stopped thread needn't be stopped; an "immune" thread
		needn't be stopped either; a non-stopped thread must otherwise be stopped, however
*/
static int thread_must_be_stopped(thread_t tid) {
	// check for immunity
	int ithread;
	for(ithread = 0; ithread < immune_nthreads_g; ithread++) 
		if(immune_threads_g[ithread] == tid)
			return 0;
	// check thread state
	char thread_stat_path[MAX_PROC_PATH + 1];
	snprintf(thread_stat_path, MAX_PROC_PATH + 1, "/proc/self/task/%d/stat", (int)tid);
	int stat_fd = open(thread_stat_path, O_RDONLY, 0);
	if(stat_fd >= 0) {
		// scanf can't be used, as it requires FILE*, which must be malloc'ed, so we
		// simply read up to closing ), and then miss the space. Exec names containing 
		// ')' are unsupported, unfortunately
		char thread_state = '\0';
		char buffer[BUFFER_SIZE];
		int next_char_is_state = 0;
		unsigned nchars_in_buffer = 0;
		unsigned buffer_ptr = 0;
		while(1) {
			if(buffer_ptr >= nchars_in_buffer) {
				nchars_in_buffer = read(stat_fd, buffer, BUFFER_SIZE);
				buffer_ptr = 0;
				if(!nchars_in_buffer)
					break;
			}  // if(read from buffer)
			char c = buffer[buffer_ptr++];
			if(next_char_is_state) {
				thread_state = c;
				break;
			} else if(c == ')')
				next_char_is_state = 1;			
		}  // while(1)
		close(stat_fd);
		if(!thread_state) {
			fprintf(stderr, "thread_must_be_stopped: stat_fd = %d\n", stat_fd);
			fprintf(stderr, "thread_must_be_stopped: can\'t get thread %d state\n", 
							(int)tid);
			return 0;
		} else 
			return thread_state != 'Z' && thread_state != 'S';
	} else
		return 0;
}  // thread_must_be_stopped

void stop_other_threads(void) {

	nstopped_threads_g = 0;
	// get current thread id and process id's
	thread_t my_tid = gettid();
	pid_t my_pid = getpid();

	// directory of threads for the current process
	char task_dir_path[] = "/proc/self/task";
	// indicates first iteration of "stopping threads"
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
				//int stop_this_thread = stop_every_thread;
				//if(!stop_every_thread) {
					// check status of this thread
					// TODO: do the actual check - currently simply assume it is not needed
				int stop_this_thread = thread_must_be_stopped(other_tid);
					//}
				if(stop_this_thread) {
					running_thread_found = 1;
					tgkill(my_pid, (pid_t)other_tid, SIGSTOP);
					// add to array of stopped threads
					int must_add_to_stopped = 1;
					unsigned ithread;
					for(ithread = 0; ithread < nstopped_threads_g; ithread++)
						if(stopped_threads_g[ithread] == other_tid) {
							must_add_to_stopped = 0;
							break;
						}
					if(must_add_to_stopped) {
						if(nstopped_threads_g < MAX_NTHREADS) {
							stopped_threads_g[nstopped_threads_g] = other_tid;
							nstopped_threads_g++;
						} else {
							fprintf(stderr, "stop_other_threads: too many threads, some may "
											"fail to resume\n");
						}
					}  // if(must_add_to_stopped)
				}  // if(stop_this_thread)
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
}  // stop_other_threads()

void cont_other_threads(void) {
	pid_t my_pid = getpid();
	// get current thread id and process id's
	unsigned ithread;
	for(ithread = 0; ithread < nstopped_threads_g; ithread++)
		tgkill(my_pid, stopped_threads_g[ithread], SIGCONT);
	nstopped_threads_g = 0;
	// stopped threads have been resumed
}  // cont_other_threads

#endif
