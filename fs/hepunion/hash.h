/**
 * \file hash.h
 * \brief Separated header file for hash function
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 03-Aug-2012
 * \copyright GNU General Public License - GPL
 *
 * The MurmurHash64A implementation code comes from smhasher project
 * that can be found on: http://code.google.com/p/smhasher
 * The specific code can be found at:
 * http://code.google.com/p/smhasher/source/browse/branches/chandlerc_dev/MurmurHash2.cpp
 *
 * The implementation was realised by Austin Appleby. It was slightly modified to
 * match current coding style.
 */

#include <linux/types.h>

/**
 * Check some property at compile time.
 * If the property is not true, build will fail 
 * \param[in]	e	The expression to check
 * \param[in]	m	The message to be displayed in case of a failure
 */
#define static_assert(e, m) typedef char __static_assert_##m##__[(e) ? 1 : -1]

/* Those compile time assertions are mandatory to ensure that we are running
 * on a compatible plateform.
 * The unsigned long type used for inode reference must be the same size than
 * the uint64_t hash produced
 * And int size must be 4 bytes
 * See the implemtation site for more information
 */
static_assert(sizeof(unsigned long) == sizeof(uint64_t), size_no_match);
static_assert(sizeof(int) == 4, int_no_match);

/**
 * Computes the hash of a given buffer using the MurmurHash2 function
 * with 64 bits big output.
 * \param[in]	key		The data buffer to hash
 * \param[in]	len		Size of data in the buffer
 * \param[in]	seed	Seed to use while hashing
 * \return	The computed hash
 */
uint64_t murmur_hash_64a(const void *key, int len, uint64_t seed);
