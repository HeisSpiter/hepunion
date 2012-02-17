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

#include "pierrefs.h"

struct dentry * pierrefs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nameidata) {
	/* We are looking for "dentry" in "dir" */
	int err;
	size_t len;
	char path[PATH_MAX];
	char real_path[PATH_MAX];
	struct inode *inode = NULL;

	/* First get path of the directory */
	err = get_relative_path(dir, 0, path);
	if (err < 0) {
		return ERR_PTR(err);
	}

	len = strlen(path);

	/* Now, look for the file */
	strncat(path, dentry->d_name.name, PATH_MAX - len - 1);
	err = find_file(path, real_path, 0);
	if (err < 0) {
		return ERR_PTR(err);
	}

	/* We've got it!
	 * Set dentry operations
	 */
	dentry->d_op = &pierrefs_dops;
	/* Set our inode */
	d_add(dentry, inode);

	return NULL;
}

int pierrefs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstbuf) {
	int err;
	char path[PATH_MAX];

	/* Get path */
	err = get_relative_path(0, dentry, path);
	if (err < 0) {
		return err;
	}

	/* Call worker */
	return get_file_attr(path, kstbuf);
}

int pierrefs_permission(struct inode *inode, int mask, struct nameidata *nd) {
	int err;
	char path[PATH_MAX];
	char real_path[PATH_MAX];

	/* Get path */
	err = get_relative_path(0, nd->dentry, path);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, 0);
	if (err < 0) {
		return err;
	}

	/* And call worker */
	return can_access(path, real_path, mask);
}

int pierrefs_setattr(struct dentry *dentry, struct iattr *attr) {
	int err;
	char path[PATH_MAX];
	char real_path[PATH_MAX];

	/* Get path */
	err = get_relative_path(0, dentry, path);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, 0);
	if (err < 0) {
		return err;
	}

	if (err == READ_WRITE || err == READ_WRITE_COPYUP) {
		/* Just update file attributes */
		return notify_change(dentry, attr);
    }

	/* Update me
	 * Don't clear flags, set_me_worker will do
	 * So, only call the worker
	 */
	return set_me_worker(path, real_path, attr);
}

struct inode_operations pierrefs_iops = {
	.getattr	= pierrefs_getattr,
	.lookup		= pierrefs_lookup,
	.permission	= pierrefs_permission,
	.setattr	= pierrefs_setattr,
};

struct super_operations pierrefs_sops = {
};

struct dentry_operations pierrefs_dops = {
};
