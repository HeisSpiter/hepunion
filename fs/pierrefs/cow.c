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

static int copy_child(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	char tmp_path[PATH_MAX];
	char tmp_ro_path[PATH_MAX];
	char tmp_rw_path[PATH_MAX];
	struct readdir_context *ctx = (struct readdir_context*)buf;

	pr_info("copy_child: %p, %s, %d, %llx, %llx, %d\n", buf, name, namlen, offset, ino, d_type);

	/* Don't copy special entries */
	if (is_special(name, namlen)) {
		return 0;
	}

	if (snprintf(tmp_ro_path, PATH_MAX, "%s/%s", ctx->ro_path, name) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	if (snprintf(tmp_path, PATH_MAX, "%s/%s", ctx->path, name) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Recreate everything recursively */
	return create_copyup(tmp_path, tmp_ro_path, tmp_rw_path, ctx->context);
}

int create_copyup(const char *path, const char *ro_path, char *rw_path, struct pierrefs_sb_info *context) {
	/* Once here, two things are sure:
	 * RO exists, RW does not
	 */
	int err, len;
	char tmp[PATH_MAX];
	char me_path[PATH_MAX];
	struct kstat kstbuf;
	struct file *ro_fd, *rw_fd;
	ssize_t rcount;
	unsigned char buf[MAXSIZE];
	struct dentry *dentry;
	struct iattr attr;
	struct readdir_context ctx;

	pr_info("create_copyup: %s, %s, %s, %p\n", path, ro_path, rw_path, context);

	/* Get file attributes */
	err = get_file_attr_worker(path, ro_path, context, &kstbuf);
	if (err < 0) {
		return err;
	}

	/* Copyup dirs if required */
	err = find_path(path, rw_path, context);
	if (err < 0) {
		return err;
	}

	/* Handle the file properly */
	switch (kstbuf.mode & S_IFMT) {
		/* Symbolic link */
		case S_IFLNK:
			/* Read destination */
			len = readlink(ro_path, tmp, context, sizeof(tmp) - 1);
			if (len < 0) {
				return len;
			}
			tmp[len] = '\0';

			/* And create a new symbolic link */
			err = symlink_worker(tmp, rw_path, context);
			if (err < 0) {
				return err;
			}
			break;

		/* Regular file */
		case S_IFREG:
			/* Open read only... */
			ro_fd = open_worker(ro_path, context, O_RDONLY);
			if (IS_ERR(ro_fd)) {
				return PTR_ERR(ro_fd);
			}

			/* Then, create copyup... */
			rw_fd = open_worker_2(rw_path, context, O_CREAT | O_WRONLY | O_EXCL, kstbuf.mode); 
			if (IS_ERR(rw_fd)) {
				filp_close(ro_fd, 0);
				return PTR_ERR(rw_fd);
			}


			if (kstbuf.size > 0) {
				/* Here we could use mmap. But since we are reading and writing
				 * in a non random way, read & write are faster (read ahead, lazy-write)
				 */
				for (;;) {
					push_root();
					rcount = vfs_read(ro_fd, buf, MAXSIZE, &ro_fd->f_pos);
					pop_root();
					if (rcount < 0) {
						break;
					}

					push_root();
					rcount = vfs_write(rw_fd, buf, rcount, &rw_fd->f_pos);
					pop_root();
					if (rcount < 0) {
						push_root();
						filp_close(ro_fd, 0);
						filp_close(rw_fd, 0);
						pop_root();

						/* Delete copyup */
						unlink(rw_path, context);

						return rcount;
					}
				}
			}

			/* Close files */
			push_root();
			filp_close(ro_fd, 0);
			filp_close(rw_fd, 0);
			pop_root();
			break;

		case S_IFSOCK:
		case S_IFBLK:
		case S_IFCHR:
			/* Recreate a node */
			err = mknod_worker(rw_path, context, kstbuf.mode, kstbuf.rdev);
			if (err < 0) {
				return err;
			}
			break;

		case S_IFDIR:
			/* Recreate a dir */
			err = mkdir_worker(rw_path, context, kstbuf.mode);
			if (err < 0) {
				return err;
			}

			/* Recreate dir structure */
			ro_fd = open_worker(ro_path, context, O_RDONLY);
			if (IS_ERR(ro_fd)) {
				unlink(rw_path, context);
				return PTR_ERR(ro_fd);
			}

			/* Create a copyup of each file & dir */
			ctx.ro_path = ro_path;
			ctx.path = path;
			ctx.context = context;
			push_root();
			err = vfs_readdir(ro_fd, copy_child, &ctx);
			filp_close(ro_fd, 0);
			pop_root();

			/* Handle failure */
			if (err < 0) {
				unlink(rw_path, context);
				return err;
			}

			break;

		case S_IFIFO:
			/* Recreate FIFO */
			err = mkfifo_worker(rw_path, context, kstbuf.mode);
			if (err < 0) {
				return err;
			}
			break;
	}

	/* Get dentry for the copyup */
	dentry = get_path_dentry(rw_path, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Set copyup attributes */
	attr.ia_valid = ATTR_ATIME | ATTR_MTIME | ATTR_UID | ATTR_GID | ATTR_MODE;
	attr.ia_atime = kstbuf.atime;
	attr.ia_mtime = kstbuf.mtime;
	attr.ia_uid = kstbuf.uid;
	attr.ia_gid = kstbuf.gid;
	attr.ia_mode = kstbuf.mode;

	push_root();
	err = notify_change(dentry, &attr);
	pop_root();

	if (err < 0) {
		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);
		return err;
	}

	dput(dentry);

	/* Check if there was a me and remove */
	if (find_me(path, context, me_path, &kstbuf) >= 0) {
		unlink(me_path, context);
	}

	return 0;
}

int find_path_worker(const char *path, char *real_path, struct pierrefs_sb_info *context) {
	/* Try to find that tree */
	int err;
	char read_only[PATH_MAX];
	char tree_path[PATH_MAX];
	char real_tree_path[PATH_MAX];
	types tree_path_present;
	char *old_directory;
	char *directory;
	struct kstat kstbuf;
	struct iattr attr;
	struct dentry *dentry;
	/* Get path without rest */
	char *last = strrchr(path, '/');

	pr_info("find_path_worker: %s, %s, %p\n", path, real_path, context);

	if (!last) {
		return -EINVAL;
	}

	memcpy(tree_path, path, last - path + 1);
	tree_path[last - path + 1] = '\0';
	tree_path_present = find_file(tree_path, real_tree_path, context, 0);
	/* Path should at least exist RO */
	if (tree_path_present < 0) {
		return -tree_path_present;
	}
	/* Path is already present, nothing to do */
	else if (tree_path_present == READ_WRITE) {
		/* Execpt filing in output buffer */
		if (snprintf(real_path, PATH_MAX, "%s%s", context->read_write_branch, path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		return 0;
	}

	/* Once here, recreating tree by COW is mandatory */
	if (snprintf(real_path, PATH_MAX, "%s/", context->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Also prepare for RO branch */
	if (snprintf(read_only, PATH_MAX, "%s/", context->read_only_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* If that's last (creating dir at root) */
	if (last == path) {
		if (snprintf(real_path, PATH_MAX, "%s/", context->read_write_branch) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		return 0;
	}

	/* Really get directory */
	old_directory = (char *)path + 1;
	directory = strchr(old_directory, '/');
	while (directory) {
		/* Append... */
		strncat(read_only, old_directory, (directory - old_directory) / sizeof(char));
		strncat(real_path, old_directory, (directory - old_directory) / sizeof(char));

		/* Only create if it doesn't already exist */
		if (lstat(real_path, context, &kstbuf) < 0) {
			/* Get previous dir properties */
			err = lstat(read_only, context, &kstbuf);
			if (err < 0) {
				return err;
			}

			/* Create directory */
			err = mkdir_worker(real_path, context, kstbuf.mode);
			if (err < 0) {
				return err;
			}

			/* Now, set all the previous attributes */
			dentry = get_path_dentry(real_path, context, LOOKUP_DIRECTORY);
			if (IS_ERR(dentry)) {
				/* FIXME: Should delete in case of failure */
				return PTR_ERR(dentry);
			}

			attr.ia_valid = ATTR_ATIME | ATTR_MTIME | ATTR_UID | ATTR_GID;
			attr.ia_atime = kstbuf.atime;
			attr.ia_mtime = kstbuf.mtime;
			attr.ia_uid = kstbuf.uid;
			attr.ia_gid = kstbuf.gid;

			push_root();
			err = notify_change(dentry, &attr);

			if (err < 0) {
				vfs_rmdir(dentry->d_inode, dentry);
				dput(dentry);
				return err;
			}

			dput(dentry);
		}
		pop_root();

		/* Next iteration (skip /) */
		old_directory = directory;
		directory = strchr(directory + 1, '/');
	}

	/* Append name to create */
	strcat(real_path, last);

	/* It's over */
	return 0;
}

int find_path(const char *path, char *real_path, struct pierrefs_sb_info *context) {
	pr_info("find_path: %s, %s, %p\n", path, real_path, context);

	if (real_path) {
		return find_path_worker(path, real_path, context);
	}
	else {
		char tmp_path[PATH_MAX];
		return find_path_worker(path, tmp_path, context);
	}
}

int unlink_copyup(const char *path, const char *copyup_path, struct pierrefs_sb_info *context) {
	int err;
	struct kstat kstbuf;
	char real_path[PATH_MAX];

	pr_info("unlink_copyup: %s, %s\n", path, copyup_path);

	/* First get copyup attributes */
	err = lstat(copyup_path, context, &kstbuf);
	if (err < 0) {
		return err;
	}

	/* Then unlink it */
	err = unlink(copyup_path, context);
	if (err < 0) {
		return err;
	}

	/* Now, find RO file */
	if (find_file(path, real_path, context, 0) == -ENOENT) {
		/* File doesn't exist anylonger?
		 * Don't bother and work less
		 */
		return 0;
	}

	/* Create me if required */
	return set_me(path, real_path, &kstbuf, context, MODE | TIME | OWNER);
}
