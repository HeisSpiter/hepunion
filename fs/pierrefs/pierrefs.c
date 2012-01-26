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

int pierrefs_permission(struct inode *inode, int mask, struct nameidata *nd) {
	int err;
	char path[MAX_PATH];
	char real_path[MAX_PATH];

	/* Get path */
	err = get_relative_path(0, nd->dentry, path);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, 0)
	if (err < 0) {
		return err;
	}

	/* And call worker */
	return can_access(path, real_path, mask);
}

int pierrefs_setattr(struct dentry *dentry, struct iattr *attr) {
	int err;
	char path[MAX_PATH];
	char real_path[MAX_PATH];

	/* Get path */
	err = get_relative_path(0, nd->dentry, path);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, 0)
	if (err < 0) {
		return err;
	}

	if (err == READ_WRITE || err = READ_WRITE_COPYUP) {
		/* Just update file attributes */
		return notify_change(dentry->d_inode, attr);
    }

	/* Update me
	 * Don't clear flags, set_me_worker will do
	 * So, only call the worker
	 */
	return set_me_worker(path, real_path, attr);
}
