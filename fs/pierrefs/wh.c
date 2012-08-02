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

static int hide_entry(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type);

static int check_whiteout(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	char wh_path[PATH_MAX];
	char file_path[PATH_MAX];
	struct readdir_context *ctx = (struct readdir_context*)buf;

	/* Get file path */
	if (snprintf(file_path, PATH_MAX, "%s%s", ctx->path, name) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Look for whiteout - return 1 for non-existant */
	return (find_whiteout(file_path, ctx->context, wh_path) < 0);
}

static int check_writable(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	return is_whiteout(name, namlen);
}

static int create_whiteout_worker(const char *wh_path, struct pierrefs_sb_info *context) {
	int err;
	struct iattr attr;
	struct dentry *dentry;

	/* Create file */
	struct file *fd = creat_worker(wh_path, S_IRUSR);
	if (IS_ERR(fd)) {
		return PTR_ERR(fd);
	}

	/* Set owner to root */
	attr.ia_valid = ATTR_UID | ATTR_GID;
	attr.ia_gid = 0;
	attr.ia_uid = 0;

	push_root();
	err = notify_change(fd->f_dentry, &attr);
	pop_root();
	if (err == 0) {
		return err;
	}

	/* Save dentry */
	dentry = fd->f_dentry;
	dget(dentry);

	/* Close file and delete it */
	push_root();
	filp_close(fd, 0);
	vfs_unlink(fd->f_dentry->d_inode, fd->f_dentry);
	pop_root();

	dput(dentry);

	return err;
}

int create_whiteout(const char *path, char *wh_path, struct pierrefs_sb_info *context) {
	int err;

	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(wh_path, PATH_MAX, "%s", context->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(wh_path, path, tree_path - path + 1);
	/* Append wh */
	strcat(wh_path, ".wh.");
	/* Finalement copy name */
	strcat(wh_path, tree_path + 1);

	/* Ensure path exists */
	err = find_path(path, NULL, context);
	if (err < 0) {
		return err;
	}

	/* Call worker */
	return create_whiteout_worker(wh_path, context);
}

static int delete_whiteout(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	int err;
	struct dentry *dentry;
	char wh_path[PATH_MAX];
	struct readdir_context *ctx = (struct readdir_context*)buf;
	struct pierrefs_sb_info *context = ctx->context;

	/* assert(is_whiteout(name, namlen)); */

	/* Get whiteout path */
	if (snprintf(wh_path, PATH_MAX, "%s%s", ctx->path, name) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Get its dentry */
	dentry = get_path_dentry(wh_path, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Remove file */
	push_root();
	err = vfs_unlink(dentry->d_inode, dentry);
	pop_root();
	dput(dentry);

	return err;
}

int find_whiteout(const char *path, struct pierrefs_sb_info *context, char *wh_path) {
	int err;
	struct kstat kstbuf;

	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(wh_path, PATH_MAX, "%s", context->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(wh_path, path, tree_path - path + 1);
	/* Append me */
	strcat(wh_path, ".wh.");
	/* Finally copy name */
	strcat(wh_path, tree_path + 1);

	/* Does it exists */
	push_root();
	err = vfs_lstat(wh_path, &kstbuf);
	pop_root();

	return err;
}

int hide_directory_contents(const char *path, struct pierrefs_sb_info *context) {
	int err;
	struct file *ro_fd;
	struct kstat kstbuf;
	char rw_path[PATH_MAX];
	char ro_path[PATH_MAX];

	if (snprintf(ro_path, PATH_MAX, "%s%s", context->read_only_branch, path) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* If RO even does not exist, all correct */
	err = vfs_lstat(ro_path, &kstbuf);
	if (err < 0) {
		if (err == -ENOENT) {
			return 0;
		} else {
			return err;
		}
	}

	if (snprintf(rw_path, PATH_MAX, "%s%s", context->read_write_branch, path) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	ro_fd = open_worker(ro_path, O_RDONLY);
	if (IS_ERR(ro_fd)) {
		return PTR_ERR(ro_fd);
	}

	/* Hide all entries */
	push_root();
	err = vfs_readdir(ro_fd, hide_entry, rw_path);
	filp_close(ro_fd, 0);
	pop_root();

	return err;
}

static int hide_entry(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	char wh_path[PATH_MAX];
	struct readdir_context *ctx = (struct readdir_context*)buf;

	if (snprintf(wh_path, PATH_MAX, "%s/.wh.%s", ctx->path, name) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	return create_whiteout_worker(wh_path, ctx->context);
}

int is_empty_dir(const char *path, const char *ro_path, const char *rw_path, struct pierrefs_sb_info *context) {
	int err;
	struct file *ro_fd;
	struct file *rw_fd;
	struct readdir_context ctx;

	ro_fd = open_worker(ro_path, O_RDONLY);
	if (IS_ERR(ro_fd)) {
		return PTR_ERR(ro_fd);
	}

	ctx.path = path;
	ctx.context = context;
	push_root();
	err = vfs_readdir(ro_fd, check_whiteout, &ctx);
	filp_close(ro_fd, 0);
	pop_root();

	/* Return if an error occured or if the RO branch isn't empty */
	if (err <= 0) {
		return err;
	}

	if (rw_path) {
		rw_fd = open_worker(rw_path, O_RDONLY);
		if (IS_ERR(rw_fd)) {
			return PTR_ERR(rw_fd);
		}

		push_root();
		err = vfs_readdir(rw_fd, check_writable, 0);

		/* Return if an error occured or if the RW branch isn't empty */
		if (err <= 0) {
			filp_close(rw_fd, 0);
			pop_root();
			return err;
		}

		/* Now cleanup all the whiteouts */
		ctx.path = rw_path;
		ctx.context = context;
		vfs_readdir(rw_fd, delete_whiteout, &ctx);
		filp_close(rw_fd, 0);
		pop_root();
	}

	return err;
}

int unlink_rw_file(const char *path, const char *rw_path, struct pierrefs_sb_info *context, char has_ro_sure) {
	int err;
	char has_ro = 0;
	char ro_path[PATH_MAX];
	char wh_path[PATH_MAX];
	struct dentry *dentry;


	/* Check if RO exists */
	if (!has_ro_sure && find_file(path, ro_path, context, MUST_READ_ONLY) >= 0) {
		has_ro = 1;
	}
	else if (has_ro_sure) {
		has_ro = 1;
	}

	/* Check if user can unlink file */
	err = can_remove(path, rw_path, context);
	if (err < 0) {
		return err;
	}

	/* Get file dentry */
	dentry = get_path_dentry(rw_path, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Remove file */
	push_root();
	err = vfs_unlink(dentry->d_inode, dentry);
	pop_root();
	dput(dentry);

	if (err < 0) {
		return err;
	}

	/* Whiteout potential RO file */
	if (has_ro) {
		create_whiteout(path, wh_path, context);
	}

	return 0;
}

int unlink_whiteout(const char *path, struct pierrefs_sb_info *context) {
	int err;
	char wh_path[PATH_MAX];
	struct dentry *dentry;

	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(wh_path, PATH_MAX, "%s", context->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(wh_path, path, tree_path - path + 1);
	/* Append wh */
	strcat(wh_path, ".wh.");
	/* Finalement copy name */
	strcat(wh_path, tree_path + 1);

	/* Get file dentry */
	dentry = get_path_dentry(wh_path, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Now unlink whiteout */
	push_root();
	err = vfs_unlink(dentry->d_inode, dentry);
	pop_root();
	dput(dentry);

	return err;
}
