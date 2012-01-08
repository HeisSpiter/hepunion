/**
 * \file pierrefs.h
 * \brief Global header file included in all PierreFS files
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 21-Nov-2011
 * \copyright GNU General Public License - GPL
 * \todo Implementing caching
 *
 * PierreFS file system intend to provide a file systems unioning
 * solution that comes with several specifications:
 * - Copy-on-write
 * - Low redundancy in files
 * - Data and metadata separation
 */

#ifndef _PIERREFS_H_
#define _PIERREFS_H_

#define PIERREFS_VERSION	"1.0"
#define PIERREFS_NAME		"PierreFS"
#define PIERREFS_MAGIC		0x9F510

struct pierrefs_sb_info {
	/**
	 * Contains the full path of the RW branch
	 * \warning It is not \ terminated
	 */
	char *read_write_branch;
	size_t rw_lean;
	/**
	 * Contains the full path of the RO branch
	 * \warning It is not \ terminated
	 */
	char *read_only_branch;
	size_t ro_lean;
};

/**
 * Rights mask used to handle shifting with st_mode rights definition.
 * It allows you to skip a set of right to go to the next one.
 * First, others. One shift (on the left), group. Second shift, user
 * \sa can_access
 */
#define RIGHTS_MASK	0x3

/**
 * Mask that defines all the modes of a file that can be changed using the
 * metadata mechanism
 */
#define VALID_MODES_MASK (S_ISUID | S_ISGID | S_ISVTX |			\
			  S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR |	\
			  S_IRWXG | S_IRGRP | S_IWGRP | S_IXGRP |	\
			  S_IRWXO | S_IROTH | S_IWOTH | S_IXOTH)

/**
 * Clear the opening/creating flags that could be sent to the open
 * function
 * This only allows rights bits
 * \param[in]	f	The flags to clear
 * \return	The cleared flags
 */
#define clear_mode_flags(f) f &= VALID_MODES_MASK
/**
 * Check if in a set of flags, another set of flags is set
 * \param[in]	s	The set of flags in which to check
 * \param[in]	f	The seeked flags
 * \return	1 if all seeked flags are set, 0 otherwise
 */
#define is_flag_set(s, f) ((s & f) == f)

/**
 * \brief Enumeration defining all the possible returns of the find_file() function
 * \sa find_file
 *
 * Those are used to describe where the find_file() function found a file (if ever it
 * found one).
 */
typedef enum _types {
	/**
	 * The file was found on the RO branch
	 */
	READ_ONLY = 0,
	/**
	 * The file was found on the RW branch
	 */
	READ_WRITE = 1,
	/**
	 * The file was found on the RO branch, and a copyup has been created
	 */
	READ_WRITE_COPYUP = 2
} types;

extern struct pierrefs_sb_info *sb_info;

#endif /* #ifndef _PIERREFS_H_ */
