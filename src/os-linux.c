/** @file os-linux.c
		Linux-specific implementations of "cross-OS" functions
 */

#ifndef __APPLE__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpuvm.h"
#include "util.h"

// maximum path length for directories in /proc
#define MAX_PROC_PATH 255

// buffer size for some internal needs
#define BUFFER_SIZE 64

/** array of threads stopped by libgpuvm, linux only */
pid_t stopped_threads_g[MAX_NTHREADS];

/** number of threads stopped by libgpuvm, linux only */
unsigned nstopped_threads_g = 0;

/** a structure describing current linux directory entry */
typedef struct {
	/** inode number */
	long d_ino;
	/** offset from the start of this directory to the next directory */
	off_t d_off;
	/** length of this structure */
	unsigned short d_reclen;
	/** directory name */
	char d_name[];
} linux_dirent;

#define DIRENT_BUF_SIZE 1024
/** dirent buffer */
char dirent_buf_g[DIRENT_BUF_SIZE];
/** current dirent buffer position */
int dirent_buf_pos_g = -1;
/** current size of filled buffer */
int dirent_filled_size_g = 0;

/** getdents() linux syscall */
static int getdents(int fd, void *buf, unsigned count) {
	return syscall(SYS_getdents, fd, buf, count);
}

/** opens directory pointed to by path 
		@param path null-terminated path to directory to open
		@returns unix fd of the open directory or -1 in case of failure

 */
static int my_opendir(const char* path) {
	int fd = open(path, O_RDONLY | O_DIRECTORY);
	dirent_buf_pos_g = -1;
	dirent_filled_size_g = 0;
	if(fd < 0) {
		fprintf(stderr, "my_opendir: can\'t open directory\n");
	}
	return fd;
}

/** closes directory descriptor 
		@param fd directory descriptor previously opened by my_opendir()
 */
static void my_closedir(int fd) {
	close(fd);
}

/** reads the next directory entry name (including . and ..)
		@param fd the directory descriptor
		@returns pointer to the directory name read, or 0 if directory stream ended
		@remarks this function is neither reenterant nor thread-safe
 */
static const char* my_readdirentname(int fd) {
	if(dirent_buf_pos_g == -1 || dirent_buf_pos_g >= dirent_filled_size_g) {
		// need to read more data
		dirent_filled_size_g = getdents(fd, dirent_buf_g, DIRENT_BUF_SIZE);
		if(dirent_filled_size_g < 0) {
			fprintf(stderr, "my_readdirentname: can\'t read directory entries\n");
			return 0;
		}
		if(!dirent_filled_size_g) {
			// end of directory reached
			return 0;
		}
		dirent_buf_pos_g = 0;
	}  // if(need more buffer)
	// serve more data from buffer
	linux_dirent *cur_dirent = (linux_dirent*)(dirent_buf_g + dirent_buf_pos_g);
	dirent_buf_pos_g += cur_dirent->d_reclen;
	return cur_dirent->d_name;
}  // my_readdirentname

/** wrapper for gettid syscall */
static pid_t gettid(void) {
	return (pid_t)syscall(SYS_gettid);
}

/** wrapper for tgkill syscall */
static int tgkill(int tgid, int tid, int sig) {
	return syscall(SYS_tgkill, tgid, tid, sig);
}

thread_t self_thread() {
	return gettid();
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
	for(ithread = 0; ithread < nstopped_threads_g; ithread++)
		if(stopped_threads_g[ithread] == tid)
			return 0;
	return 1;
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
		int task_dir_fd = my_opendir(task_dir_path);
		//struct dirent *thread_dirent;
		const char *thread_dirent_name;
		while(thread_dirent_name = my_readdirentname(task_dir_fd)) {
			int other_tid;
			if(!strcmp(thread_dirent_name, ".") || !strcmp(thread_dirent_name, ".."))
				continue;
			if(sscanf(thread_dirent_name, "%d", &other_tid))	{
				if((pid_t)other_tid == my_tid)
					continue;
				int stop_this_thread = thread_must_be_stopped(other_tid);
				if(stop_this_thread) {
					running_thread_found = 1;
					//fprintf(stderr, "stopping thread %d\n", other_tid);
					tgkill(my_pid, (pid_t)other_tid, SIG_SUSP);
					if(nstopped_threads_g < MAX_NTHREADS) {
						stopped_threads_g[nstopped_threads_g] = other_tid;
						nstopped_threads_g++;
					} else {
						fprintf(stderr, "stop_other_threads: too many threads, some may "
										"fail to resume\n");
					}
				}  // if(stop_this_thread)
			} else {
				fprintf(stderr, "stop_other_threads: %s: non-numeric subdir of thread dir "
								"found\n", thread_dirent_name);
			}  // if(other_tid)
		}  // while(thread_dirent)
		my_closedir(task_dir_fd);
		if(stop_every_thread) {
			stop_every_thread = 0;
			running_thread_found = 1;
		}
	}  // end of while()
	// every thread except for the caller is stopped now
}  // stop_other_threads()

void cont_other_threads(void) {
	// get current thread id and process id's
	unsigned ithread;
	for(ithread = 0; ithread < nstopped_threads_g; ithread++)
		self_block_post();
	nstopped_threads_g = 0;
	// stopped threads have been resumed
}  // cont_other_threads

#endif
