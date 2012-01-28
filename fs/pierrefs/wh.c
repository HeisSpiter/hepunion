/**
 * \file wh.c
 * \brief Whiteout (WH) support for the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 11-Jan-2012
 * \copyright GNU General Public License - GPL
 *
 * Whiteout is the mechanism that allows file and directory
 * deletion on the read-only branch.
 *
 * When a demand to delete a file on the read-only branch is
 * made, the PierreFS file system will a matching whiteout file
 * on the read-write branch.
 *
 * That way, during union, whiteout files will be used to hide
 * some files from the read-only branch.
 *
 * Deleting the whiteout "recovers" the file.
 *
 * Whiteouts consist in files called .wh.{original file}
 *
 * This is based on the great work done by the UnionFS driver
 * team.
 */

#include "pierrefs.h"

static int create_whiteout_worker(const char *wh_path) {
	int err;
	struct iattr attr;

	/* Create file */
	struct file *fd = creat_worker(wh_path, S_IRUSR);
	if (IS_ERR(fd)) {
		return PTR_ERR(fd);
	}

	/* Set owner to root */
	attr.ia_valid = ATTR_UID | ATTR_GID;
	attr.ia_gid = 0;
	attr.ia_uid = 0;

	err = notify_change(fd->f_dentry->d_inode, &attr);
	filp_close(fd, 0);

	if (err < 0) {
		filp_close(fd, 0);
		vfs_unlink(fd->f_dentry->d_inode, fd->f_dentry);
		return err;
	}

	filp_close(fd, 0);
	return 0;
}

int create_whiteout(const char *path, char *wh_path) {
	int err;

	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(wh_path, PATH_MAX, "%s", get_context()->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(wh_path, path, tree_path - path + 1);
	/* Append wh */
	strcat(wh_path, ".wh.");
	/* Finalement copy name */
	strcat(wh_path, tree_path + 1);

	/* Ensure path exists */
	err = find_path(path, NULL);
	if (err < 0) {
		return err;
	}

	/* Call worker */
	return create_whiteout_worker(wh_path);
}

int find_whiteout(const char *path, char *wh_path) {
	struct kstat kstbuf;

	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(wh_path, PATH_MAX, "%s", get_context()->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(wh_path, path, tree_path - path + 1);
	/* Append me */
	strcat(wh_path, ".wh.");
	/* Finally copy name */
	strcat(wh_path, tree_path + 1);

	/* Does it exists */
	return vfs_lstat(wh_path, &kstbuf);
}
