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
