/**
 * \file helpers.c
 * \brief Misc functions used by the HEPunion file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 10-Dec-2011
 * \copyright GNU General Public License - GPL
 *
 * Various functions that are used at different places in
 * the driver to realize work
 */

#include "hepunion.h"


int can_access(const char *path, const char *real_path, struct hepunion_sb_info *context, int mode) {
	struct kstat stbuf;
	int err;
	uid_t fsuid;
	gid_t fsgid;
	pr_info("can_access: %s, %s, %p, %x\n", path, real_path, context, mode);

	/* Get file attributes */
	err = get_file_attr_worker(path, real_path, context, &stbuf);
	if (err) {
		return err;
	}

	/* Get IDs */
	fsuid = current_fsuid();
	fsgid = current_fsgid();

	/* If root user, allow almost everything */
	if (fsuid == 0) {
		if (mode & MAY_EXEC) {
			/* Root needs at least on X
			 * For rights details, see below
			 */
			if ((MAY_EXEC & (signed)stbuf.mode) ||
			    (MAY_EXEC << RIGHTS_MASK & (signed)stbuf.mode) ||
			    (MAY_EXEC << (RIGHTS_MASK * 2) & (signed)stbuf.mode)) {
				return 0;
			}
		}
		else {
			/* Root can read/write */
			return 0;
		}
	}

	/* Match attribute checks
	 * Here are some explanations about those "magic"
	 * values and the algorithm behind
	 * mode will be something ORed made of:
	 * 0x4 for read access		(0b100)
	 * 0x2 for write access		(0b010)
	 * 0x1 for execute access	(0b001)
	 * Modes work the same for a file
	 * But those are shifted depending on who they
	 * apply
	 * So from left to right you have:
	 * Owner, group, others
	 * It's mandatory to shift requested rights from 3/6
	 * to match actual rights
	 * Check is done from more specific to general.
	 * This explains order and values
	 */
	if (fsuid == stbuf.uid) {
		mode <<= (RIGHTS_MASK * 2);
	}
	else if (fsgid == stbuf.gid) {
		mode <<= RIGHTS_MASK;
	}

	/* Now compare bit sets and return */
	if ((mode & (signed)stbuf.mode) == mode) {
		return 0;
	}
	else {
		return -EACCES;
	}
}

int can_remove(const char *path, const char *real_path, struct hepunion_sb_info *context) {
	char *parent_path;
	int ret;
	/* Find parent directory */
	char *parent = strrchr(real_path, '/');

	pr_info("can_remove: %s, %s, %p\n", path, real_path, context);

	/* Caller wants to remove /! */
	if (parent == real_path) {
		return -EACCES;
	}

	parent_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!parent_path) {
		return -ENOMEM;
	}

	strncpy(parent_path, real_path, parent - real_path);
	parent_path[parent - real_path] = '\0';

	/* Caller must be able to write in parent dir */
	ret = can_access(path, parent_path, context, MAY_WRITE);
	kfree(parent_path);
	return ret;
}

int can_traverse(const char *path, struct hepunion_sb_info *context) {
	char *short_path = NULL, *long_path = NULL;
	int err = -ENOMEM;
	char *last, *old_directory, *directory;

	pr_info("can_traverse: %s, %p\n", path, context);

	short_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!short_path) {
		return -ENOMEM;
	}

	long_path = kmalloc(PATH_MAX, GFP_KERNEL);//dynamic allocation to avoid stack error
	if (!long_path) {
		goto cleanup;
	}

	/* Prepare strings */
	snprintf(short_path, PATH_MAX, "%c", '/');
	if (snprintf(long_path, PATH_MAX, "%s/", context->read_only_branch) > PATH_MAX) {
		err = -ENAMETOOLONG;
		goto cleanup;
	}

	/* Get directory */
	last = strrchr(path, '/');
	/* If that's last (traversing root is always possible) */
	if (path == last) {
		err = 0;
		goto cleanup;
	}

	/* Really get directory */
	old_directory = (char *)path + 1;
	directory = strchr(old_directory, '/');
	while (directory) {
		strncat(short_path, old_directory, (directory - old_directory) / sizeof(char));
		strncat(long_path, old_directory, (directory - old_directory) / sizeof(char));
		err = can_access(short_path, long_path, context, MAY_EXEC);
		if (err < 0) {
			goto cleanup;
		}

		/* Next iteration (skip /) */
		old_directory = directory;
		directory = strchr(old_directory + 1, '/');
	}

	/* If that point is reached, it can access */
	err = 0;

cleanup:
	if (short_path) {
		kfree(short_path);
	}

	if (long_path) {
		kfree(long_path);
	}

	return err;
}

int check_exist(const char *pathname, struct hepunion_sb_info *context, int flag) {
	int err;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	struct nameidata nd;
#else
	struct path path;
#endif


	pr_info("check_exist: %s, %p, %x\n", pathname, context, flag);

	push_root();
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	err = path_lookup(pathname, flag, &nd);
#else
	err = kern_path(pathname, flag, &path);
#endif
	pop_root();
	if (err) {
		return err;
	}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	path_release(&nd);
#else
	path_put(&path);
#endif

	return 0;
}

int find_file(const char *path, char *real_path, struct hepunion_sb_info *context, char flags) {
	int err = -ENOMEM;
	char *tmp_path = NULL, *wh_path = NULL;

	pr_info("find_file: %s, %p, %p, %x\n", path, real_path, context, flags);

	/* Do not check flags validity
	 * Caller can only be internal
	 * So it must be trusted
	 */
	if (!is_flag_set(flags, MUST_READ_ONLY)) {
		/* First try RW branch (higher priority) */
		if (make_rw_path(path, real_path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		err = check_exist(real_path, context, 0);
		if (err < 0) {
			if (is_flag_set(flags, MUST_READ_WRITE)) {
				return err;
			}
		}
		else {
			/* Check for access */
			err = can_traverse(path, context);
			if (err < 0) {
				return err;
			}

			return READ_WRITE;
		}
	}

	tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!tmp_path) {
		return -ENOMEM;
	}

	wh_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!wh_path) {
		goto cleanup;
	}

	/* Be smart, we might have to create a copyup */
	if (is_flag_set(flags, CREATE_COPYUP)) {
		if (make_ro_path(path, tmp_path) > PATH_MAX) {
			err = -ENAMETOOLONG;
			goto cleanup;
		}

		err = check_exist(tmp_path, context, 0);
		if (err < 0) {
			/* If file does not exist, even in RO, fail */
			goto cleanup;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, context, wh_path);
			if (err == 0) {
				err = -ENOENT;
				goto cleanup;
			}
		}

		/* Check for access */
		err = can_traverse(path, context);
		if (err < 0) {
			goto cleanup;
		}

		err = create_copyup(path, tmp_path, real_path, context);
		if (err == 0) {
			err = READ_WRITE_COPYUP;
			goto cleanup;
		}
	}
	else {
		/* It was not found on RW, try RO */
		if (make_ro_path(path, real_path) > PATH_MAX) {
			err = -ENAMETOOLONG;
			goto cleanup;
		}

		err = check_exist(real_path, context, 0);
		if (err < 0) {
			goto cleanup;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, context, wh_path);
			if (err == 0) {
				err = -ENOENT;
				goto cleanup;
			}
		}

		/* Check for access */
		err = can_traverse(path, context);
		if (err < 0) {
			goto cleanup;
		}

		/* We fall back here instead of deleting, to get the memory freed */
		err = READ_ONLY;
	}

	/* If we arrive here, it was not found at all (excepted for upper case) */
cleanup:
	if (tmp_path) {
		kfree(tmp_path);
	}

	if (wh_path) {
		kfree(wh_path);
	}

	return err;
}

int get_full_path_i(const struct inode *inode, char *real_path) {
	int len = -EBADF;
	struct dentry *dentry;
#if LINUX_VERSION_CODE != KERNEL_VERSION(2,6,18)
	struct hlist_node *entry;
#endif

	pr_info("get_full_path_i: %p, %p\n", inode, real_path);

	/* Try to browse all the dentry, till we find one nice */
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	list_for_each_entry(dentry, &inode->i_dentry, d_alias) {
#else
	hlist_for_each_entry(dentry, entry, &inode->i_dentry, d_alias) {
#endif
		/* Get full path for the given inode */
		len = get_full_path_d(dentry, real_path);
		if (len > 0) {
			/* We found the dentry! Break out */
			if (name_to_ino(real_path) == inode->i_ino) {
				break;
			}
		}
	}

	return len;
}

/* Adapted from nfs_path function */
int get_full_path_d(const struct dentry *dentry, char *real_path) {
	char *tmp_path = NULL, *end;
	int namelen = 0, buflen = PATH_MAX;
        
	pr_info("get_full_path_d: %p, %p\n", dentry, real_path);
	pr_info("Getting full path of: %s\n", dentry->d_name.name);

	tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!tmp_path) {
		return -ENOMEM;
	}
	end = tmp_path + PATH_MAX;

	*--end = '\0';
	buflen--;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	spin_lock(&dcache_lock);
#else
	/* FIXME: Use rename lock */
#endif
	while (!IS_ROOT(dentry)) {
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0) {
			buflen = -ENAMETOOLONG;
			goto cleanup;
		}
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		dentry = dentry->d_parent;
	}
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	spin_unlock(&dcache_lock);
#endif

	if (buflen == PATH_MAX - 1) {
		*--end = '/';
		buflen--;
	}

	/* Copy back name */
	memcpy(real_path, end, buflen);

	pr_info("Full path: %s\n", real_path);

cleanup:
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	spin_unlock(&dcache_lock);
#endif
	kfree(tmp_path);
	return buflen;
}

struct dentry * get_path_dentry(const char *pathname, struct hepunion_sb_info *context, int flag) {
	int err;
	struct dentry *dentry;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	struct nameidata nd;
#else
	struct path path;
#endif

	pr_info("get_path_dentry: %s, %p, %x\n", pathname, context, flag);

	push_root();
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	err = path_lookup(pathname, flag, &nd);
#else
	err = kern_path(pathname, flag, &path);
#endif
	pop_root();
	if (err) {
		return ERR_PTR(err);
	}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	dentry = nd.dentry;
#else
	dentry = path.dentry;
#endif
	dget(dentry);
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	path_release(&nd);
#else
	path_put(&path);
#endif

	return dentry;
}

int get_relative_path(const struct inode *inode, const struct dentry *dentry, const struct hepunion_sb_info *context, char *path, int is_ours) {
	int len;
	char *real_path;

	pr_info("get_relative_path: %p, %p, %p, %p, %d\n", inode, dentry, context, path, is_ours);

	real_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!real_path) {
		return -ENOMEM;
	}

	/* First, get full path */
	if (dentry) {
		len = get_full_path_d(dentry, real_path);
	} else {
		len = get_full_path_i(inode, real_path);
	}
	if (len < 0) {
		goto cleanup;
	}

	/* If those structures are owned by HEPunion, there's no
	 * need to skip the branch part
	 */
	if (is_ours) {
		memcpy(path, real_path, len);
		len = 0;
		goto cleanup;
	}

	/* Check if it's on RO */
	if (strncmp(context->read_only_branch, real_path, context->ro_len) == 0) {
		memcpy(path, real_path + 1 + context->ro_len, len - 1 - context->ro_len);
		len = 0;
		goto cleanup;
	}

	/* Check if it's on RW */
	if (strncmp(context->read_write_branch, real_path, context->rw_len) == 0) {
		memcpy(path, real_path + 1 + context->rw_len, len - 1 - context->rw_len);
		len = 0;
		goto cleanup;
	}

	len = -EINVAL;

cleanup:
	kfree(real_path);
	return len;
}

int get_relative_path_for_file(const struct inode *dir, const struct dentry *dentry, const struct hepunion_sb_info *context, char *path, int is_ours) {
	int err;
	size_t len;

	pr_info("get_relative_path_for_file: %p, %p, %p, %p, %d\n", dir, dentry, context, path, is_ours);

	/* First get path of the directory */
	err = get_relative_path(dir, NULL, context, path, is_ours);
	if (err < 0) {
		return err;
	}

	len = strlen(path);
	/* Ensure it can fit in */
	if (len + dentry->d_name.len > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Now, look for the file */
	strncat(path, dentry->d_name.name, PATH_MAX - len - 1);

	return 0;
}

int path_to_special(const char *path, specials type, const struct hepunion_sb_info *context, char *outpath) {
	size_t len = strlen(path);
	char *tree_path = strrchr(path, '/');
	size_t written = 0;

	pr_info("path_to_special: %s, %d, %p\n", path, type, outpath);

	if (!tree_path) {
		return -EINVAL;
	}

	/* Ensure the complete path can fit in the output path */
	if (context->rw_len + len + 5 > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	memcpy(outpath, context->read_write_branch, context->rw_len);
	written = context->rw_len;

	if (written + tree_path - path + 5 > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	memcpy(outpath + written, path, tree_path - path + 1);
	written += tree_path - path + 1;

	/* Append me or wh */
	if (type == ME) {
		memcpy(outpath + written, ".me.", 4);
	} else {
		memcpy(outpath + written, ".wh.", 4);
	}
	written += 4;

	/* Finalement copy name */
	memcpy(outpath + written, tree_path + 1, path + len - tree_path);
	written += path + len - tree_path;
	outpath[written] = 0;

	return 0;
}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
/* Imported for Linux kernel and simplified */
int lstat(const char *pathname, struct hepunion_sb_info *context, struct kstat *stat) {
	struct nameidata nd;
	int error;

	pr_info("lstat: %s, %p\n", pathname, stat);

	push_root();
	error = path_lookup(pathname, 0, &nd);
	pop_root();
	if (!error) {
		push_root();
		error = vfs_getattr(nd.mnt, nd.dentry, stat);
		pop_root();
		path_release(&nd);
	}

	return error;
}
#else
/* Imported for Linux kernel and simplified */
int lstat(const char *pathname, struct hepunion_sb_info *context, struct kstat *stat) {
	struct path path;
	int error = -EINVAL;
	unsigned int lookup_flags = 0;

	pr_info("lstat: %s, %p\n", pathname, stat);

retry:
	push_root();
	error = kern_path(pathname, lookup_flags, &path);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	error = vfs_getattr(path.mnt, path.dentry, stat);
	pop_root();
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}

	return error;
}
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
/* Imported for Linux kernel */
long mkdir(const char *pathname, struct hepunion_sb_info *context, int mode) {
	int error = 0;
	struct dentry *dentry;
	struct nameidata nd;

	pr_info("mkdir: %s, %p, %x\n", pathname, context, mode);

	push_root();
	error = path_lookup(pathname, LOOKUP_PARENT, &nd);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	dentry = lookup_create(&nd, 1);
	pop_root();
	error = PTR_ERR(dentry);

	if (!IS_ERR(dentry)) {
		if (!IS_POSIXACL(nd.dentry->d_inode)) {
			mode &= ~current->fs->umask;
		}
		push_root();
		error = vfs_mkdir(nd.dentry->d_inode, dentry, mode);
		pop_root();
		dput(dentry);
	}
	mutex_unlock(&nd.dentry->d_inode->i_mutex);
	path_release(&nd);

	return error;
}
#else
/* Imported for Linux kernel */
long mkdir(const char *pathname, struct hepunion_sb_info *context, umode_t mode) {
	struct dentry *dentry;
	struct path path;
	int error;
	unsigned int lookup_flags = LOOKUP_DIRECTORY;

	pr_info("mkdir: %s, %p, %x\n", pathname, context, mode);

retry:
	push_root();
	dentry = kern_path_create(AT_FDCWD, pathname, &path, lookup_flags);
	pop_root();
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	if (!IS_POSIXACL(path.dentry->d_inode)) {
		mode &= ~current_umask();
	}
	push_root();
	error = security_path_mkdir(&path, dentry, mode);
	if (!error) {
		error = vfs_mkdir(path.dentry->d_inode, dentry, mode);
	}
	pop_root();
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
/* Imported from Linux kernel */
long mknod(const char *pathname, struct hepunion_sb_info *context, int mode, unsigned dev) {
	int error = 0;
	struct dentry * dentry;
	struct nameidata nd;

	pr_info("mknod: %s, %p, %x, %u\n", pathname, context, mode, dev);

	if (S_ISDIR(mode))
		return -EPERM;

	push_root();
	error = path_lookup(pathname, LOOKUP_PARENT, &nd);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	dentry = lookup_create(&nd, 0);
	pop_root();
	error = PTR_ERR(dentry);

	if (!IS_POSIXACL(nd.dentry->d_inode))
		mode &= ~current->fs->umask;
	if (!IS_ERR(dentry)) {
		switch (mode & S_IFMT) {
			case 0: case S_IFREG:
				push_root();
				error = vfs_create(nd.dentry->d_inode,dentry,mode,&nd);
				pop_root();
				break;
			case S_IFCHR: case S_IFBLK:
				push_root();
				error = vfs_mknod(nd.dentry->d_inode,dentry,mode, new_decode_dev(dev));
				pop_root();
				break;
			case S_IFIFO: case S_IFSOCK:
				push_root();
				error = vfs_mknod(nd.dentry->d_inode,dentry,mode,0);
				pop_root();
				break;
			case S_IFDIR:
				error = -EPERM;
				break;
			default:
				error = -EINVAL;
		}
		dput(dentry);
	}
	mutex_unlock(&nd.dentry->d_inode->i_mutex);
	path_release(&nd);

	return error;
}
#else
/* Imported from Linux kernel */
long mknod(const char *pathname, struct hepunion_sb_info *context, umode_t mode, unsigned dev) {
	struct dentry *dentry;
	struct path path;
	int error;
	unsigned int lookup_flags = 0;

	pr_info("mknod: %s, %p, %x, %u\n", pathname, context, mode, dev);

retry:
	push_root();
	dentry = kern_path_create(AT_FDCWD, pathname, &path, lookup_flags);
	pop_root();
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	if (!IS_POSIXACL(path.dentry->d_inode)) {
		mode &= ~current_umask();
	}
	push_root();
	error = security_path_mknod(&path, dentry, mode, dev);
	if (error) {
		goto out;
	}
	switch (mode & S_IFMT) {
		case 0: case S_IFREG:
			error = vfs_create(path.dentry->d_inode, dentry, mode, true);
			break;
		case S_IFCHR: case S_IFBLK:
			error = vfs_mknod(path.dentry->d_inode, dentry, mode,
					new_decode_dev(dev));
			break;
		case S_IFIFO: case S_IFSOCK:
			error = vfs_mknod(path.dentry->d_inode, dentry, mode, 0);
			break;
	}
	pop_root();
out:
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
int mkfifo(const char *pathname, struct hepunion_sb_info *context, int mode) {
#else
int mkfifo(const char *pathname, struct hepunion_sb_info *context, umode_t mode) {
#endif
	pr_info("mkfifo: %s, %p, %x\n", pathname, context, mode);

	/* Ensure FIFO mode is set */
	mode |= S_IFIFO;

	/* Call mknod */
	return mknod(pathname, context, mode, 0);
}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
/* Imported from Linux kernel */
long symlink(const char *oldname, const char *newname, struct hepunion_sb_info *context) {
	int error = 0;
	struct dentry *dentry;
	struct nameidata nd;

	pr_info("symlink: %s, %s, %p\n", oldname, newname, context);

	push_root();
	error = path_lookup(newname, LOOKUP_PARENT, &nd);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	dentry = lookup_create(&nd, 0);
	pop_root();
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		push_root();
		error = vfs_symlink(nd.dentry->d_inode, dentry, oldname, S_IALLUGO);
		pop_root();
		dput(dentry);
	}
	mutex_unlock(&nd.dentry->d_inode->i_mutex);
	path_release(&nd);

	return error;
}
#else
/* Imported from Linux kernel */
long symlink(const char *oldname, const char *newname, struct hepunion_sb_info *context) {
	int error;
	struct dentry *dentry;
	struct path path;
	unsigned int lookup_flags = 0;

	pr_info("symlink: %s, %s, %p\n", oldname, newname, context);

retry:
	push_root();
	dentry = kern_path_create(AT_FDCWD, newname, &path, lookup_flags);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		return error;
	}

	error = security_path_symlink(&path, dentry, oldname);
	if (!error) {
		error = vfs_symlink(path.dentry->d_inode, dentry, oldname);
	}
	pop_root();
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}

	return error;
}
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
/* Imported from Linux kernel - simplified */
long link(const char *oldname, const char *newname, struct hepunion_sb_info *context) {
	struct dentry *new_dentry;
	struct nameidata nd, old_nd;
	int error;

	pr_info("link: %s, %s, %p\n", oldname, newname, context);

	push_root();
	error = path_lookup(oldname, 0, &old_nd);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	error = path_lookup(newname, LOOKUP_PARENT, &nd);
	pop_root();
	if (error)
		goto out;
	error = -EXDEV;
	if (old_nd.mnt != nd.mnt)
		goto out_release;
	push_root();
	new_dentry = lookup_create(&nd, 0);
	pop_root();
	error = PTR_ERR(new_dentry);
	if (!IS_ERR(new_dentry)) {
		push_root();
		error = vfs_link(old_nd.dentry, nd.dentry->d_inode, new_dentry);
		pop_root();
		dput(new_dentry);
	}
	mutex_unlock(&nd.dentry->d_inode->i_mutex);
out_release:
	path_release(&nd);
out:
	path_release(&old_nd);

	return error;
}
#else
/* Imported from Linux kernel - simplified */
long link(const char *oldname, const char *newname, struct hepunion_sb_info *context) {
	struct dentry *new_dentry;
	struct path old_path, new_path;
	int how = 0;
	int error;

	pr_info("link: %s, %s, %p\n", oldname, newname, context);

retry:
	push_root();
	error = kern_path(oldname, how, &old_path);
	pop_root();
	if (error) {
		return error;
	}

	push_root();
	new_dentry = kern_path_create(AT_FDCWD, newname, &new_path, (how & LOOKUP_REVAL));
	pop_root();
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry)) {
		goto out;
	}

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt) {
		goto out_dput;
	}
	push_root();
	error = security_path_link(old_path.dentry, &new_path, new_dentry);
	pop_root();
	if (error) {
		goto out_dput;
	}
	push_root();
	error = vfs_link(old_path.dentry, new_path.dentry->d_inode, new_dentry);
	pop_root();
out_dput:
	done_path_create(&new_path, new_dentry);
	if (retry_estale(error, how)) {
		how |= LOOKUP_REVAL;
		goto retry;
	}
out:
	path_put(&old_path);

	return error;
}
#endif

/* Imported from Linux kernel */
long readlink(const char *path, char *buf, struct hepunion_sb_info *context, int bufsiz) {
	struct inode *inode;
	int error;
	mm_segment_t oldfs;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	struct nameidata nd;
#else
	struct path spath;
#endif

	pr_info("readlink: %s, %p, %p, %d\n", path, buf, context, bufsiz);

	if (bufsiz <= 0)
		return -EINVAL;

	push_root();
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	error = path_lookup(path, 0, &nd);
#else
	error = kern_path(path, 0, &spath);
#endif
	pop_root();
	if (!error) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		inode = nd.dentry->d_inode;
#else
		inode = spath.dentry->d_inode;
#endif
		error = -EINVAL;
		if (inode->i_op && inode->i_op->readlink) {
			push_root();
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
			error = security_inode_readlink(nd.dentry);
#else
			error = security_inode_readlink(spath.dentry);
#endif
			if (!error) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
				touch_atime(nd.mnt, nd.dentry);
				call_usermode();
				error = inode->i_op->readlink(nd.dentry, buf, bufsiz);
#else
				touch_atime(&spath);
				call_usermode();
				error = inode->i_op->readlink(spath.dentry, buf, bufsiz);
#endif
				restore_kernelmode();
			}
			pop_root();
		}
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		path_release(&nd);
#else
		path_put(&spath);
#endif
	}
	return error;
}

long rmdir(const char *pathname, struct hepunion_sb_info *context) {
	int err;
	short lookup = 0;
	struct inode *dir;
	struct dentry *dentry;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	struct nameidata nd;
#else
	struct path path;
#endif

	pr_info("rmdir: %s, %p\n", pathname, context);

	/* Get dir dentry */
	dentry = get_path_dentry(pathname, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Get parent inode */
	dir = dentry->d_parent->d_inode;
	if (dir == NULL) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		err = path_lookup(pathname, LOOKUP_PARENT, &nd);
#else
		err = kern_path(pathname, LOOKUP_PARENT, &path);
#endif
		if (err) {
			dput(dentry);
			return err;
		}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		dir = nd.dentry->d_inode;
#else
		dir = path.dentry->d_inode;
#endif
		lookup = 1;
	}

	/* Remove directory */
	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);
	push_root();
	err = vfs_rmdir(dir, dentry);
	pop_root();
	mutex_unlock(&dir->i_mutex);
	if (lookup) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		path_release(&nd);
#else
		path_put(&path);
#endif
	}
	dput(dentry);

	return err;
}

long unlink(const char *pathname, struct hepunion_sb_info *context) {
	int err;
	short lookup = 0;
	struct inode *dir;
	struct dentry *dentry;
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
	struct nameidata nd;
#else
	struct path path;
#endif
	pr_info("unlink: %s, %p\n", pathname, context);

	/* Get file dentry */
	dentry = get_path_dentry(pathname, context, LOOKUP_REVAL);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	}

	/* Get parent inode */
	dir = dentry->d_parent->d_inode;
	if (dir == NULL) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		err = path_lookup(pathname, LOOKUP_PARENT, &nd);
#else
		err = kern_path(pathname, LOOKUP_PARENT, &path);
#endif
		if (err) {
			dput(dentry);
			return err;
		}

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		dir = nd.dentry->d_inode;
#else
		dir = path.dentry->d_inode;
#endif
		lookup = 1;
	}

	/* Remove file */
	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);
	push_root();
	err = vfs_unlink(dir, dentry);
	pop_root();
	mutex_unlock(&dir->i_mutex);
	if (lookup) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18)
		path_release(&nd);
#else
		path_put(&path);
#endif
	}
	dput(dentry);

	return err;
}

#ifdef _DEBUG_
struct file* dbg_open(const char *pathname, const struct hepunion_sb_info *context, int flags) {
	pr_info("dbg_open: %s, %p, %x\n", pathname, context, flags);

	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
			pr_err("Attempted to write on RO branch!\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, 0);
}

struct file* dbg_open_2(const char *pathname, const struct hepunion_sb_info *context, int flags, mode_t mode) {
	pr_info("dbg_open_2: %s, %p, %x, %x\n", pathname, context, flags, mode);

	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
			pr_err("Attempted to write on RO branch!\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, mode);
}

struct file* dbg_creat(const char *pathname, const struct hepunion_sb_info *context, mode_t mode) {
	pr_info("dbg_creat: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return ERR_PTR(-EINVAL);
	}

	return filp_creat(pathname, mode);
}

int dbg_mkdir(const char *pathname, struct hepunion_sb_info *context, mode_t mode) {
	pr_info("dbg_mkdir: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mkdir(pathname, context, mode);
}

int dbg_mknod(const char *pathname, struct hepunion_sb_info *context, mode_t mode, dev_t dev) {
	pr_info("dbg_mknod: %s, %p, %x, %x\n", pathname, context, mode, dev);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mknod(pathname, context, mode, dev);
}

int dbg_mkfifo(const char *pathname, struct hepunion_sb_info *context, mode_t mode) {
	pr_info("dbg_mkfifo: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mkfifo(pathname, context, mode);
}

int dbg_symlink(const char *oldpath, const char *newpath, struct hepunion_sb_info *context) {
	pr_info("dbg_symlink: %s, %s, %p\n", oldpath, newpath, context);

	if (strncmp(newpath, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return symlink(oldpath, newpath, context);
}

int dbg_link(const char *oldpath, const char *newpath, struct hepunion_sb_info *context) {
	pr_info("dbg_link: %s, %s, %p\n", oldpath, newpath, context);

	if (strncmp(newpath, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return link(oldpath, newpath, context);
}
#endif
