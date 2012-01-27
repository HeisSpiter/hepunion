/**
 * \file cow.c
 * \brief Copy-On-Write (COW) support for the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 11-Jan-2012
 * \copyright GNU General Public License - GPL
 *
 * Copy-on-write (often written COW) is the mechanism that allows
 * files of the read-only branch modification. When someone needs
 * to modify (and can) a file, then a copy of the file (called 
 * copyup) is created in the read-write branch.
 *
 * Next, when the user reads the file, priority is given to the
 * copyups.
 *
 * COW process is also used on directories.
 *
 * Unlike all the other implementations of file system unions,
 * in PierreFS, copyup are not created when an attempt to change
 * file metadata is done. Metadata are handled separately. This
 * reduces copyup use.
 *
 * Unlike all the other implementations of file system unions,
 * the PierreFS file system will do its best to try to reduce
 * redundancy by removing copyup when it appears they are useless
 * (same contents than the original file).
 *
 * This is based on the great work done by the UnionFS driver
 * team.
 */

#include "pierrefs.h"

int create_copyup(const char *path, const char *ro_path, char *rw_path) {
	return -1;
}

int find_path_worker(const char *path, char *real_path) {
	return -1;
}

int find_path(const char *path, char *real_path) {
	if (real_path) {
		return find_path_worker(path, real_path);
	}
	else {
		char tmp_path[PATH_MAX];
		return find_path_worker(path, tmp_path);
	}
}
