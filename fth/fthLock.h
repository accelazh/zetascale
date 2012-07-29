/*
 * File:   fthLock.h
 * Author: Jim
 *
 * Created on February 29, 2008
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http: //www.schoonerinfotech.com/
 *
 * $Id: fthLock.h 396 2008-02-29 22:55:43Z jim $
 */

//
// Schooner locks for FTH-type threads
//

#include "platform/defs.h"
#include "fthSpinLock.h"

#ifndef _FTH_LOCK_H
#define _FTH_LOCK_H

/** @brief The lock data structure for sleep-type read-write locks */
typedef struct fthLock {
    pthread_mutex_t     mutex;
    const char * name;                       // file and line number, for now
    const char * func;                       // __PRETTY_FUNCTION__
} fthLock_t;

typedef fthLock_t fthWaitEl_t;

__BEGIN_DECLS
// Routines - must be here because of wait El definition
/**
 * @brief Initialize lock
 * @param lock <OUT> lock to initialize
 */
void fthLockInitName(struct fthLock *lock, const char *name, const char *f);

#define fth__s(s) #s
#define fth_s(s) fth__s(s)

#define fthLockInit(lock) \
	  fthLockInitName((lock), __FILE__ ":" fth_s(__LINE__), __PRETTY_FUNCTION__)

/**
 * @brief Lock.
 *
 * For fth<->fth locking.  Use #fthXLock/#pthreadXLock or #fthMutexLock for
 * pthread<->fth locking.
 *
 * @param lock <IN> lock to acquire
 * @param write <IN> 0 for read lock, non-zero for write
 * @return fthWaitEl_t * to pass to fthUnlock.
 */
fthWaitEl_t *fthLock(struct fthLock *lock, int write, fthWaitEl_t *waitEl);
fthWaitEl_t *fthTryLock(struct fthLock *lock, int write, fthWaitEl_t *waitEl);

/**
 * @brief Release lock
 *
 * @param waitEl <IN> waiter returned by fthLock or fthTryLock.
 */ 
void fthUnlock(fthWaitEl_t *waitEl);
void fthDemoteLock(fthWaitEl_t *lockEl);
__END_DECLS

#endif
