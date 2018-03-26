/*
 * Copyright 2018 Stacy <stacy@sks.uz> 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifndef NOFAIL_WRAPPERS_LOADED_
#define NOFAIL_WRAPPERS_LOADED_
/* 
 * wrappers for various functions 
 * that always will ensure that function will
 * either return or the application will abort 
 */

#include <pthread.h>



#ifndef NOFAIL_MUTEX_UNNEEDED
static void mutex_lock(pthread_mutex_t *mutex);
static void mutex_unlock(pthread_mutex_t *mutex);
#endif
#ifndef NOFAIL_LOCK_UNNEEDED
static void lock_read(pthread_rwlock_t *rwlock);
static void lock_unlock(pthread_rwlock_t *rwlock);
static void lock_write(pthread_rwlock_t *rwlock);
#endif

#ifndef NOFAIL_MUTEX_UNNEEDED
static void mutex_lock(pthread_mutex_t *mutex)
{
	int	res;
	
	assert(mutex);
	res = pthread_mutex_lock(mutex);
	if (res) SYMFATAL("cannot lock mutex: %s\n", STRERROR_R(res));
	assert(res == 0);
}

static void mutex_unlock(pthread_mutex_t *mutex)
{
	int	res;
	
	assert(mutex);
	res = pthread_mutex_unlock(mutex);
	if (res) SYMFATAL("cannot unlock mutex: %s\n", STRERROR_R(res));
	assert(res == 0);
}
#endif

#ifndef NOFAIL_LOCK_UNNEEDED
static void lock_read(pthread_rwlock_t *rwlock)
{
	int	res;
	
	assert(rwlock);
	res = pthread_rwlock_rdlock(rwlock);
	if (res) SYMFATAL("cannot readlock rwlock: %s\n", STRERROR_R(res));
	assert(res == 0);
}

static void lock_unlock(pthread_rwlock_t *rwlock)
{
	int	res;
	
	assert(rwlock);
	res = pthread_rwlock_unlock(rwlock);
	if (res) SYMFATAL("cannot unlock rwlock: %s\n", STRERROR_R(res));
	assert(res == 0);
}

static void lock_write(pthread_rwlock_t *rwlock)
{
	int	res;
	
	assert(rwlock);
	res = pthread_rwlock_wrlock(rwlock);
	if (res) SYMFATAL("cannot writelock rwlock: %s\n", STRERROR_R(res));
	assert(res == 0);
}
#endif

#endif /* NOFAIL_WRAPPERS_LOADED_ */

