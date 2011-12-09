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
	char * read_write_branch;
	/**
	 * Contains the full path of the RO branch
	 * \warning It is not \ terminated
	 */
	char * read_only_branch;
};

/**
 * Check if in a set of flags, another set of flags is set
 * \param[in]	s	The set of flags in which to check
 * \param[in]	f	The seeked flags
 * \return	1 if all seeked flags are set, 0 otherwise
 */
#define is_flag_set(s, f) ((s & f) == f)

#endif /* #ifndef _PIERREFS_H_ */
