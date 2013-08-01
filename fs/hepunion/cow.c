/**
 * \file cow.c
 * \brief Copy-On-Write (COW) support for the HEPunion file system
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
 * in HEPunion, copyup are not created when an attempt to change
 * file metadata is done. Metadata are handled separately. This
 * reduces copyup use.
 *
 * Unlike all the other implementations of file system unions,
 * the HEPunion file system will do its best to try to reduce
 * redundancy by removing copyup when it appears they are useless
 * (same contents than the original file).
 *
 * This is based on the great work done by the UnionFS driver
 * team.
 */

#include "hepunion.h"

static int copy_child(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	int ret = -ENOMEM;
	struct readdir_context *ctx = (struct readdir_context*)buf;
    char *tmp_path = NULL, *tmp_ro_path = NULL, *tmp_rw_path = NULL;

	pr_info("copy_child: %p, %s, %d, %llx, %llx, %d\n", buf, name, namlen, offset, ino, d_type);

	/* Don't copy special entries */
	if (is_special(name, namlen)) {
		return 0;
	}

	/* Dynamic allocations to avoid stack errors
	 * TODO: Optimize later on with lookaside allocations
	 */
	tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!tmp_path) {
		return -ENOMEM;
	}

	tmp_ro_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!tmp_ro_path) {
		goto cleanup;
	}

	tmp_rw_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!tmp_rw_path) {
		goto cleanup;
	}

	if (snprintf(tmp_ro_path, PATH_MAX, "%s/%s", ctx->ro_path, name) > PATH_MAX) {
		ret = -ENAMETOOLONG;
		goto cleanup;
	}

	if (snprintf(tmp_path, PATH_MAX, "%s/%s", ctx->path, name) > PATH_MAX) {
		ret = -ENAMETOOLONG;
		goto cleanup;
	}

	/* Recreate everything recursively */
	ret = create_copyup(tmp_path, tmp_ro_path, tmp_rw_path, ctx->context);

cleanup:
	if (tmp_path) {
		kfree(tmp_path);
	}

	if (tmp_ro_path) {
		kfree(tmp_ro_path);
	}

	if (tmp_rw_path) {
		kfree(tmp_rw_path);
	}

	return ret;
}

int create_copyup(const char *path, const char *ro_path, char *rw_path, struct hepunion_sb_info *context) {
	 /* Once here, two things are sure:
	 * RO exists, RW does not
	 */
	int err = -ENOMEM, len;	 
	char *tmp = NULL, *me_path = NULL, *buf = NULL;
	struct kstat kstbuf;
	struct file *ro_fd, *rw_fd;
	ssize_t rcount;
	struct dentry *dentry;
	struct iattr attr;
	struct readdir_context ctx;
	mm_segment_t oldfs;

	pr_info("create_copyup: %s, %s, %s, %p\n", path, ro_path, rw_path, context);

	tmp = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!tmp) {
		return -ENOMEM;
	}

	me_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!me_path) {
		goto cleanup;
	}

	buf = kmalloc(MAXSIZE, GFP_KERNEL);
	if(!buf) {
		goto cleanup;
	}

	/* Get file attributes */
	err = get_file_attr_worker(path, ro_path, context, &kstbuf);
	if (err < 0) {
		goto cleanup;
	}

	/* Copyup dirs if required */
	err = find_path(path, rw_path, context);
	if (err < 0) {
		goto cleanup;
	}

	/* Handle the file properly */
	switch (kstbuf.mode & S_IFMT) {
		/* Symbolic link */
		case S_IFLNK:
			/* Read destination */
			len = readlink(ro_path, tmp, context, sizeof(tmp) - 1);
			if (len < 0) {
				err = len;
				goto cleanup;
			}
			tmp[len] = '\0';

			/* And create a new symbolic link */
			err = symlink_worker(tmp, rw_path, context);
			if (err < 0) {
				goto cleanup;
			}
			break;

		/* Regular file */
		case S_IFREG:
			/* Open read only... */
			ro_fd = open_worker(ro_path, context, O_RDONLY);
			if (IS_ERR(ro_fd)) {
				err = PTR_ERR(ro_fd);
				goto cleanup;
			}

			/* Then, create copyup... */
			rw_fd = open_worker_2(rw_path, context, O_CREAT | O_WRONLY | O_EXCL, kstbuf.mode); 
			if (IS_ERR(rw_fd)) {
				filp_close(ro_fd, NULL);
				err = PTR_ERR(rw_fd);
				goto cleanup;
			}


			if (kstbuf.size > 0) {
				/* Here we could use mmap. But since we are reading and writing
				 * in a non random way, read & write are faster (read ahead, lazy-write)
				 */
				for (;;) {
					push_root();
					call_usermode();
					rcount = vfs_read(ro_fd, buf, MAXSIZE, &ro_fd->f_pos);
					restore_kernelmode();
					pop_root();
					if (rcount < 0) {
						push_root();
						filp_close(ro_fd, NULL);
						filp_close(rw_fd, NULL);
						pop_root();

						/* Delete copyup */
						unlink(rw_path, context);

						err = rcount;
						goto cleanup;
					} else if (rcount == 0) {
						break;
					}

					push_root();
					call_usermode();
					rcount = vfs_write(rw_fd, buf, rcount, &rw_fd->f_pos);
					restore_kernelmode();
					pop_root();
					if (rcount < 0) {
						push_root();
						filp_close(ro_fd, NULL);
						filp_close(rw_fd, NULL);
						pop_root();

						/* Delete copyup */
						unlink(rw_path, context);

						err = rcount;
						goto cleanup;
					}
				}
			}

			/* Close files */
			push_root();
			filp_close(ro_fd, NULL);
			filp_close(rw_fd, NULL);
			pop_root();
			break;

		case S_IFSOCK:
		case S_IFBLK:
		case S_IFCHR:
			/* Recreate a node */
			err = mknod_worker(rw_path, context, kstbuf.mode, kstbuf.rdev);
			if (err < 0) {
				goto cleanup;
			}
			break;

		case S_IFDIR:
			/* Recreate a dir */
			err = mkdir_worker(rw_path, context, kstbuf.mode);
			if (err < 0) {
				goto cleanup;
			}

			/* Recreate dir structure */
			ro_fd = open_worker(ro_path, context, O_RDONLY);
			if (IS_ERR(ro_fd)) {
				unlink(rw_path, context);
				err = PTR_ERR(ro_fd);
				goto cleanup;
			}

			/* Create a copyup of each file & dir */
			ctx.ro_path = ro_path;
			ctx.path = path;
			ctx.context = context;
			push_root();
			err = vfs_readdir(ro_fd, copy_child, &ctx);
			filp_close(ro_fd, NULL);
			pop_root();

			/* Handle failure */
			if (err < 0) {
				unlink(rw_path, context);
				goto cleanup;
			}

			break;

		case S_IFIFO:
			/* Recreate FIFO */
			err = mkfifo_worker(rw_path, context, kstbuf.mode);
			if (err < 0) {
				goto cleanup;
			}
			break;
	}

	/* Get dentry for the copyup */
	dentry = get_path_dentry(rw_path, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto cleanup;
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
		vfs_unlink(dentry->d_parent->d_inode, dentry);
		pop_root();
		dput(dentry);
		goto cleanup;
	}

	dput(dentry);

	/* Check if there was a me and remove */
	if (find_me(path, context, me_path, &kstbuf) >= 0) {
		unlink(me_path, context);
	}

	err = 0;
cleanup:
	if (tmp) {
		kfree(tmp);
	}

	if (me_path) {
		kfree(me_path);
	}

	if (buf) {
		kfree(buf);
	}

	return err;
}

static int find_path_worker(const char *path, char *real_path, struct hepunion_sb_info *context) {
	/* Try to find that tree */
	int err = -ENOMEM;
	char *read_only = NULL, *tree_path = NULL, *real_tree_path = NULL;
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

	read_only = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!read_only) {
		return -ENOMEM;
	}

	tree_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!tree_path) {
		goto cleanup;
	}

	real_tree_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!real_tree_path) {
		goto cleanup;
	}

	memcpy(tree_path, path, last - path + 1);
	tree_path[last - path + 1] = '\0';
	tree_path_present = find_file(tree_path, real_tree_path, context, 0);
	/* Path should at least exist RO */
	if (tree_path_present < 0) {
		err = -tree_path_present;
		goto cleanup;
	}
	/* Path is already present, nothing to do */
	else if (tree_path_present == READ_WRITE) {
		/* Execpt filing in output buffer */
		if (snprintf(real_path, PATH_MAX, "%s%s", context->read_write_branch, path) > PATH_MAX) {
			err = -ENAMETOOLONG;
			goto cleanup;
		}

		err = 0;
		goto cleanup;
	}

	/* Once here, recreating tree by COW is mandatory */
	if (snprintf(real_path, PATH_MAX, "%s/", context->read_write_branch) > PATH_MAX) {
		err = -ENAMETOOLONG;
		goto cleanup; 
	}

	/* Also prepare for RO branch */
	if (snprintf(read_only, PATH_MAX, "%s/", context->read_only_branch) > PATH_MAX) {
		err = -ENAMETOOLONG;
		goto cleanup;
	}

	/* If that's last (creating dir at root) */
	if (last == path) {
		if (snprintf(real_path, PATH_MAX, "%s/", context->read_write_branch) > PATH_MAX) {
			err = -ENAMETOOLONG;
			goto cleanup;
		}

		err = 0;
		goto cleanup;
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
				goto cleanup;
			}

			/* Create directory */
			err = mkdir_worker(real_path, context, kstbuf.mode);
			if (err < 0) {
				goto cleanup;
			}

			/* Now, set all the previous attributes */
			dentry = get_path_dentry(real_path, context, LOOKUP_DIRECTORY);
			if (IS_ERR(dentry)) {
				/* FIXME: Should delete in case of failure */
				err = PTR_ERR(dentry);
				goto cleanup;
			}

			attr.ia_valid = ATTR_ATIME | ATTR_MTIME | ATTR_UID | ATTR_GID;
			attr.ia_atime = kstbuf.atime;
			attr.ia_mtime = kstbuf.mtime;
			attr.ia_uid = kstbuf.uid;
			attr.ia_gid = kstbuf.gid;

			push_root();
			err = notify_change(dentry, &attr);

			if (err < 0) {
				vfs_rmdir(dentry->d_parent->d_inode, dentry);
				dput(dentry);
				goto cleanup;
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
	err = 0;

cleanup:
	if (read_only) {
		kfree(read_only);
	}

	if (tree_path) {
		kfree(tree_path);
	}

	if (real_tree_path) {
		kfree(real_tree_path);
	}

	return err;
}

int find_path(const char *path, char *real_path, struct hepunion_sb_info *context) {
	int ret;

	pr_info("find_path: %s, %s, %p\n", path, real_path, context);
        
	if (real_path) {
		return find_path_worker(path, real_path, context);
	}
	else {
		char *tmp_path;
		tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
		if(!tmp_path) {
			return -ENOMEM;
		}
                
		ret = find_path_worker(path, tmp_path, context);
		kfree(tmp_path); 
		return ret;
	}
}

int unlink_copyup(const char *path, const char *copyup_path, struct hepunion_sb_info *context) {
	int err;
	char *real_path;
	struct kstat kstbuf;

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

	real_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if(!real_path) {
		return -ENOMEM;
	}

	/* Now, find RO file */
	if (find_file(path, real_path, context, 0) == -ENOENT) {
		/* File doesn't exist anylonger?
		 * Don't bother and work less
		 */
		kfree(real_path);
		return 0;
	}

	/* Create me if required */
	err = set_me(path, real_path, &kstbuf, context, MODE | TIME | OWNER);
	kfree(real_path);
	return err;
}
