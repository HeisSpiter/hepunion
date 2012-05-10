/**
 * \file recursivemutex.h
 * \brief Separated header file for recursive mutex
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 13-Mar-2011
 * \copyright GNU General Public License - GPL
 *
 * This recursive mutex is a reentrant lock mechanism. It means
 * that a thread that already owns the mutex can reacquire it without
 * having any wait delay.
 * Then, the mutex is only released when the thread doesn't need
 * mutex any more (all the locks are gone).
 *
 * The implementation of the mutex is really simple. Issues might
 * raise.
 */

#include <linux/spinlock.h>
#include <linux/sched.h>

typedef struct {
	atomic_t			count;
	struct thread_info	*owner;
	spinlock_t			lock;
} recursive_mutex_t;

/**
 * Initialise a recursive mutex.
 * \param[out]	mutex	The mutex to initialise
 * \return	Nothing
 */
void recursive_mutex_init(recursive_mutex_t *mutex);
/**
 * Lock a recursive mutex. If the mutex is already locked
 * by the mutex, this function returns immediately. Otherwise
 * the caller is blocked.
 * \param[int]	mutex	The mutex to lock
 * \return	Nothing
 */
void recursive_mutex_lock(recursive_mutex_t *mutex);
/**
 * Unlock a recursive mutex.
 * \param[int]	mutex	The mutex to unlock
 * \return	Nothing
 */
void recursive_mutex_unlock(recursive_mutex_t *mutex);
