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

struct dentry * pierrefs_lookup(struct inode *inode, struct dentry *dentry, struct nameidata *nameidata) {
	return 0;
}

int pierrefs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstbuf) {
	int err;
	char path[MAX_PATH];

	/* Get path */
	err = get_relative_path(0, dentry, path);
	if (err) {
		return err;
	}

	/* Call worker */
	return get_file_attr(path, kstbuf);
}

int pierrefs_permission(struct inode *inode, int mask) {
	int err;
	char path[MAX_PATH];
	char real_path[MAX_PATH];

	/* Get path */
	err = get_relative_path(inode, 0, path);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, 0)
	if (err) {
		return err;
	}

	/* And call worker */
	return can_access(path, real_path, mask);
}
