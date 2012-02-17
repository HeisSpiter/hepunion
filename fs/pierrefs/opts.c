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

int pierrefs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) {
	int err, origin;
	char from[PATH_MAX];
	char to[PATH_MAX];
	char real_from[PATH_MAX];
	char real_to[PATH_MAX];

	/* First, find file */
	err = get_relative_path(0, old_dentry, from);
	if (err < 0) {
		return err;
	}

	origin = find_file(from, real_from, 0);
	if (origin < 0) {
		return origin;
	}

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, to);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	if (origin == READ_ONLY) {
		/* Here, fallback to a symlink */
		err = symlink_worker(real_from, real_to);
		if (err < 0) {
			return err;
		}
	}
	else {
		/* Get RW name */
		if (make_rw_path(to, real_to) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		err = link_worker(real_from, real_to);
		if (err < 0) {
			return err;
		}
	}

	/* Remove possible whiteout */
	unlink_whiteout(to);

	return 0;
}

struct dentry * pierrefs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nameidata) {
	/* We are looking for "dentry" in "dir" */
	int err;
	char path[PATH_MAX];
	char real_path[PATH_MAX];
	struct inode *inode = NULL;

	/* First get path of the file */
	err = get_relative_path_for_file(dir, dentry, path);
	if (err < 0) {
		return ERR_PTR(err);
	}

	/* Now, look for the file */
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

int pierrefs_symlink(struct inode *dir, struct dentry *dentry, const char *symname) {
	/* Create the link on the RW branch */
	int err;
	char to[PATH_MAX];
	char real_to[PATH_MAX];

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, to);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	/* Get full path for destination */
	if (make_rw_path(to, real_to) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Not it's sure the link does not exist, create it */
	err = symlink_worker(symname, real_to);
	if (err < 0) {
		return err;
	}

	/* Remove possible whiteout */
	unlink_whiteout(to);

	return 0;
}

struct inode_operations pierrefs_iops = {
	.getattr	= pierrefs_getattr,
	.link		= pierrefs_link,
	.lookup		= pierrefs_lookup,
	.permission	= pierrefs_permission,
	.setattr	= pierrefs_setattr,
	.symlink	= pierrefs_symlink,
};

struct super_operations pierrefs_sops = {
};

struct dentry_operations pierrefs_dops = {
};
