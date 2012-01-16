#ifndef GPUVM_SEMAPH_H_
#define GPUVM_SEMAPH_H_

/** @file semaph.h contains platform-independent interface for
		semaphores. Required because Darwin does not support industry-standard POSIX
		semaphores, supported by Linux */

#ifndef __APPLE__
#include <semaphore.h>
typedef sem_t semaph_t;
#else
#include <mach/semaphore.h>
typedef semaphore_t semaph_t;
#endif

/** initializes a new semaphore with the initial value
		@param sem the semaphore
		@param value initial value for the semaphore
		@returns 0 if successful and a negative error code if not
 */
int semaph_init(semaph_t *sem, int value);

/** post on a semaphore (up)
		@param sem the semaphore
		@returns 0 if successful and a negative error code if not
 */
int semaph_post(semaph_t *sem);

/** waits on a semaphore (down)
		@param sem the semaphore 
		@returns 0 if successful and a negative error code if not
 */
int semaph_wait(semaph_t *sem);

/** destroys a semaphore 
		@param sem the semaphore 
		@returns 0 if successful and a negative error code if not
*/
int semaph_destroy(semaph_t *sem);

#endif
