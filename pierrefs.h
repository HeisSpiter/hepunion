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

/**
 * Rights mask used to handle shifting with st_mode rights definition.
 * It allows you to skip a set of right to go to the next one.
 * First, others. One shift (on the left), group. Second shift, user
 * \sa can_access
 */
#define RIGHTS_MASK	0x3

/**
 * Flag to pass to find_file() function. It indicates that if the file was
 * found RO a copyup has to be done and its path returned
 * \sa find_file
 */
#define CREATE_COPYUP	0x1
/**
 * Flag to pass to find_file() function. It indicates that the file has to
 * already exist on the RW branch. If it doesn't, the function will fail
 * \sa find_file
 */
#define MUST_READ_WRITE	0x2
/**
 * Flag to pass to find_file() function. The function will only check the
 * RO branch to find the function. If it doesn't exist there, the function
 * will fail (even if it could have existed on RW branch)
 * \sa find_file
 */
#define MUST_READ_ONLY	0x4
/**
 * Flag to pass to find_file() function. It indicates that the file that
 * the function will return might not exist regarding union method
 * \sa find_file
 */
#define IGNORE_WHITEOUT	0x8

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
 * Generate the string matching the given path for a full RO path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RO path
 * \return	The number of caracters written to r
 */
#define make_ro_path(p, r) snprintf(r, PATH_MAX, "%s%s", sb_info->read_only_branch, p)
/**
 * Generate the string matching the given path for a full RW path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RW path
 * \return	The number of caracters written to r
 */
#define make_rw_path(p, r) snprintf(r, PATH_MAX, "%s%s", sb_info->read_write_branch, p)

extern struct pierrefs_sb_info *sb_info;

/* Functions in helpers.c */
/**
 * Check Read/Write/Execute permissions on a file for calling process.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	real_path	Full path of the file to check
 * \param[in]	mode		ORed set of modes to check (R_OK, W_OK, X_OK)
 * \return	1 if calling process can access, 0 otherwise. errno is set
 * \note	This is checked against user, group, others permissions
 */
char can_access(const char *path, const char *real_path, int mode);
/**
 * Check permission for the calling process to create a file.
 * \param[in]	p	Relative path of the file to create
 * \param[in]	rp	Full path of the file to create
 * \return	1 if calling process can create, 0 otherwise. errno is set
 * \note	This is just a wrapper to can_remove since required rights the same
 */
#define can_create(p, rp) can_remove(p, rp)
/**
 * Check permission for the calling process to remove a file.
 * \param[in]	path		Relative path of the file to remove
 * \param[in]	real_path	Full path of the file to remove
 * \return	1 if calling process can remove, 0 otherwise. errno is set
 * \note	This is checked against user, group, others permissions for writing in parent directory
 */
char can_remove(const char *path, const char *real_path);
/**
 * Check permission for the calling process to go through a tree.
 * \param[in]	path	Relative path of the tree to traverse
 * \return	1 if calling process can remove, 0 otherwise. errno is set
 * \note	This is checked against user, group, others permissions for execute in traverse directories
 */
char can_traverse(const char *path);
/**
 * Find a file either in RW or RO branch, taking into account whiteout files. It can copyup files if needed.
 * \param[in]	path		Relative path of the file to find
 * \param[out]	real_path	Full path of the file, if found
 * \param[in]	flags		ORed set of flags defining where and how finding file (CREATE_COPYUP, MUST_READ_WRITE, MUST_READ_ONLY, IGNORE_WHITEOUT)
 * \return	-1 in case of a failure, an unsigned integer describing where the file was found in case of a success
 * \note	Unless flags state the contrary, the RW branch is the first checked for the file
 * \note	In case you called the function with CREATE_COPYUP flag, and it succeded, then returned path is to RW file
 * \warning	There is absolutely no checks for flags consistency!
 */
types find_file(const char *path, char *real_path, char flags);

#endif /* #ifndef _PIERREFS_H_ */
