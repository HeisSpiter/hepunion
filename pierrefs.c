/**
 * \file pierrefs.c
 * \brief Exported functions by the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 10-Dec-2011
 * \copyright GNU General Public License - GPL
 * \todo Disallow .me. and .wh. files creation
 * \todo Identical files on RO/RW after mod
 */

int pierrefs_permission(struct inode *inode, int mask) {
	int err;

	/* Get file... */
	err = find_file(/*path, real_path*/, 0)
	if (err) {
		return err;
	}

	/* And call worker */
	return can_access(/*path, real_path*/, mask);
}
