/**
 * \file helpers.c
 * \brief Misc functions used by the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 10-Dec-2011
 * \copyright GNU General Public License - GPL
 *
 * Various functions that are used at different places in
 * the driver to realize work
 */

#include "pierrefs.h"

int can_access(const char *path, const char *real_path, struct pierrefs_sb_info *context, int mode) {
	struct kstat stbuf;
	int err;

	pr_info("can_access: %s, %s, %p, %x\n", path, real_path, context, mode);

	/* Get file attributes */
	err = get_file_attr_worker(path, real_path, context, &stbuf);
	if (err) {
		return err;
	}

	/* If root user, allow almost everything */
	if (current->fsuid == 0) {
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
	if (current->fsuid == stbuf.uid) {
		mode <<= (RIGHTS_MASK * 2);
	}
	else if (current->fsgid == stbuf.gid) {
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

int can_remove(const char *path, const char *real_path, struct pierrefs_sb_info *context) {
	char parent_path[PATH_MAX];

	/* Find parent directory */
	char *parent = strrchr(real_path, '/');

	pr_info("can_remove: %s, %s, %p\n", path, real_path, context);

	/* Caller wants to remove /! */
	if (parent == real_path) {
		return -EACCES;
	}

	strncpy(parent_path, real_path, parent - real_path);
	parent_path[parent - real_path] = '\0';

	/* Caller must be able to write in parent dir */
	return can_access(path, parent_path, context, MAY_WRITE);
}

int can_traverse(const char *path, struct pierrefs_sb_info *context) {
	char short_path[PATH_MAX];
	char long_path[PATH_MAX];
	int err;
	char *last, *old_directory, *directory;

	pr_info("can_traverse: %s, %p\n", path, context);

	/* Prepare strings */
	snprintf(short_path, PATH_MAX, "%c", '/');
	if (snprintf(long_path, PATH_MAX, "%s/", context->read_only_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Get directory */
	last = strrchr(path, '/');
	/* If that's last (traversing root is always possible) */
	if (path == last) {
		return 0;
	}

	/* Really get directory */
	old_directory = (char *)path + 1;
	directory = strchr(old_directory, '/');
	while (directory) {
		strncat(short_path, old_directory, (directory - old_directory) / sizeof(char));
		strncat(long_path, old_directory, (directory - old_directory) / sizeof(char));
		err = can_access(short_path, long_path, context, MAY_EXEC);
		if (err < 0) {
			return err;
		}

		/* Next iteration (skip /) */
		old_directory = directory;
		directory = strchr(old_directory + 1, '/');
	}

	/* If that point is reached, it can access */
	return 0;
}

int check_exist(const char *pathname, struct pierrefs_sb_info *context, int flag) {
	int err;
	struct nameidata nd;

	pr_info("check_exist: %s, %p, %x\n", pathname, context, flag);

	push_root();
	err = path_lookup(pathname, flag, &nd);
	pop_root();
	if (err) {
		return err;
	}

	path_release(&nd);

	return 0;
}

int find_file(const char *path, char *real_path, struct pierrefs_sb_info *context, char flags) {
	int err;
	char tmp_path[PATH_MAX];
	char wh_path[PATH_MAX];

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

	/* Be smart, we might have to create a copyup */
	if (is_flag_set(flags, CREATE_COPYUP)) {
		if (make_ro_path(path, tmp_path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		err = check_exist(tmp_path, context, 0);
		if (err < 0) {
			/* If file does not exist, even in RO, fail */
			return err;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, context, wh_path);
			if (err == 0) {
				return -ENOENT;
			}
		}

		/* Check for access */
		err = can_traverse(path, context);
		if (err < 0) {
			return err;
		}

		err = create_copyup(path, tmp_path, real_path, context);
		if (err == 0) {
			return READ_WRITE_COPYUP;
		}
	}
	else {
		/* It was not found on RW, try RO */
		if (make_ro_path(path, real_path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		err = check_exist(real_path, context, 0);
		if (err < 0) {
			return err;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, context, wh_path);
			if (err == 0) {
				return -ENOENT;
			}
		}

		/* Check for access */
		err = can_traverse(path, context);
		if (err < 0) {
			return err;
		}

		return READ_ONLY;
	}

	/* It was not found at all */
	return err;
}

int get_full_path_i(const struct inode *inode, char *real_path) {
	int len = -EBADF;
	struct dentry *dentry;
	struct list_head *entry = inode->i_dentry.next;

	pr_info("get_full_path_i: %p, %p\n", inode, real_path);

	/* Try to browse all the dentry, till we find one nice */
	while (entry != &inode->i_dentry) {
		dentry = list_entry(entry, struct dentry, d_alias);
		/* Get full path for the given inode */
		len = get_full_path_d(dentry, real_path);
		if (len > 0) {
			/* We found the dentry! Break out */
			if (name_to_ino(real_path) == inode->i_ino) {
				break;
			}
		}

		/* Jump to next dentry */
		entry = entry->next;
	}

	return len;
}

/* Adapted from nfs_path function */
int get_full_path_d(const struct dentry *dentry, char *real_path) {
	char tmp_path[PATH_MAX];
	char *end = tmp_path + PATH_MAX;
	int namelen = 0, buflen = PATH_MAX;

	pr_info("get_full_path_d: %p, %p\n", dentry, real_path);

	pr_info("Getting full path of: %s\n", dentry->d_name.name);

	*--end = '\0';
	buflen--;
	spin_lock(&dcache_lock);
	while (!IS_ROOT(dentry)) {
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong_unlock;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		dentry = dentry->d_parent;
	}
	spin_unlock(&dcache_lock);

	if (buflen == PATH_MAX - 1) {
		*--end = '/';
		buflen--;
	}

	/* Copy back name */
	memcpy(real_path, end, buflen);

	pr_info("Full path: %s\n", real_path);

	return buflen;

Elong_unlock:
	spin_unlock(&dcache_lock);
	return -ENAMETOOLONG;
}

struct dentry * get_path_dentry(const char *pathname, struct pierrefs_sb_info *context, int flag) {
	int err;
	struct dentry *dentry;
	struct nameidata nd;

	pr_info("get_path_dentry: %s, %p, %x\n", pathname, context, flag);

	push_root();
	err = path_lookup(pathname, flag, &nd);
	pop_root();
	if (err) {
		return ERR_PTR(err);
	}

	dentry = nd.dentry;
	dget(dentry);
	path_release(&nd);

	return dentry;
}

int get_relative_path(const struct inode *inode, const struct dentry *dentry, const struct pierrefs_sb_info *context, char *path, int is_ours) {
	int len;
	char real_path[PATH_MAX];

	pr_info("get_relative_path: %p, %p, %p, %p, %d\n", inode, dentry, context, path, is_ours);

	/* First, get full path */
	if (dentry) {
		len = get_full_path_d(dentry, real_path);
	} else {
		len = get_full_path_i(inode, real_path);
	}
	if (len < 0) {
		return len;
	}

	/* If those structures are owned by PierreFS, there's no
	 * need to skip the branch part
	 */
	if (is_ours) {
		memcpy(path, real_path, len);
		return 0;
	}

	/* Check if it's on RO */
	if (strncmp(context->read_only_branch, real_path, context->ro_len) == 0) {
		memcpy(path, real_path + 1 + context->ro_len, len - 1 - context->ro_len);
		return 0;
	}

	/* Check if it's on RW */
	if (strncmp(context->read_write_branch, real_path, context->rw_len) == 0) {
		memcpy(path, real_path + 1 + context->rw_len, len - 1 - context->rw_len);
		return 0;
	}

	return -EINVAL;
}

int get_relative_path_for_file(const struct inode *dir, const struct dentry *dentry, const struct pierrefs_sb_info *context, char *path, int is_ours) {
	int err;
	size_t len;

	pr_info("get_relative_path_for_file: %p, %p, %p, %p, %d\n", dir, dentry, context, path, is_ours);

	/* First get path of the directory */
	err = get_relative_path(dir, 0, context, path, is_ours);
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

int path_to_special(const char *path, specials type, const struct pierrefs_sb_info *context, char *outpath) {
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

/* Imported for Linux kernel and simplified */
int lstat(const char *pathname, struct pierrefs_sb_info *context, struct kstat *stat)
{
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

/* Imported for Linux kernel */
long mkdir(const char *pathname, struct pierrefs_sb_info *context, int mode) {
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
		if (!IS_POSIXACL(nd.dentry->d_inode))
			mode &= ~current->fs->umask;
		push_root();
		error = vfs_mkdir(nd.dentry->d_inode, dentry, mode);
		pop_root();
		dput(dentry);
	}
	mutex_unlock(&nd.dentry->d_inode->i_mutex);
	path_release(&nd);

	return error;
}

/* Imported from Linux kernel */
long mknod(const char *pathname, struct pierrefs_sb_info *context, int mode, unsigned dev) {
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

int mkfifo(const char *pathname, struct pierrefs_sb_info *context, int mode) {
	pr_info("mkfifo: %s, %p, %x\n", pathname, context, mode);

	/* Ensure FIFO mode is set */
	mode |= S_IFIFO;

	/* Call mknod */
	return mknod(pathname, context, mode, 0);
}

/* Imported from Linux kernel */
long symlink(const char *oldname, const char *newname, struct pierrefs_sb_info *context) {
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

/* Imported from Linux kernel - simplified */
long link(const char *oldname, const char *newname, struct pierrefs_sb_info *context) {
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

/* Imported from Linux kernel */
long readlink(const char *path, char *buf, struct pierrefs_sb_info *context, int bufsiz) {
	struct inode *inode;
	struct nameidata nd;
	int error;

	pr_info("readlink: %s, %p, %p, %d\n", path, buf, context, bufsiz);

	if (bufsiz <= 0)
		return -EINVAL;

	push_root();
	error = path_lookup(path, 0, &nd);
	pop_root();
	if (!error) {
		inode = nd.dentry->d_inode;
		error = -EINVAL;
		if (inode->i_op && inode->i_op->readlink) {
			push_root();
			error = security_inode_readlink(nd.dentry);
			if (!error) {
				touch_atime(nd.mnt, nd.dentry);
				error = inode->i_op->readlink(nd.dentry, buf, bufsiz);
			}
			pop_root();
		}
		path_release(&nd);
	}
	return error;
}

long unlink(const char *pathname, struct pierrefs_sb_info *context) {
	int err;
	struct dentry *dentry;

	/* Get file dentry */
	dentry = get_path_dentry(pathname, context, LOOKUP_REVAL);
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

#ifdef _DEBUG_
struct file* dbg_open(const char *pathname, const struct pierrefs_sb_info *context, int flags) {
	pr_info("dbg_open: %s, %p, %x\n", pathname, context, flags);

	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
			pr_err("Attempted to write on RO branch!\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, 0);
}

struct file* dbg_open_2(const char *pathname, const struct pierrefs_sb_info *context, int flags, mode_t mode) {
	pr_info("dbg_open_2: %s, %p, %x, %x\n", pathname, context, flags, mode);

	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
			pr_err("Attempted to write on RO branch!\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, mode);
}

struct file* dbg_creat(const char *pathname, const struct pierrefs_sb_info *context, mode_t mode) {
	pr_info("dbg_creat: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return ERR_PTR(-EINVAL);
	}

	return filp_creat(pathname, mode);
}

int dbg_mkdir(const char *pathname, struct pierrefs_sb_info *context, mode_t mode) {
	pr_info("dbg_mkdir: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mkdir(pathname, context, mode);
}

int dbg_mknod(const char *pathname, struct pierrefs_sb_info *context, mode_t mode, dev_t dev) {
	pr_info("dbg_mknod: %s, %p, %x, %x\n", pathname, context, mode, dev);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mknod(pathname, context, mode, dev);
}

int dbg_mkfifo(const char *pathname, struct pierrefs_sb_info *context, mode_t mode) {
	pr_info("dbg_mkfifo: %s, %p, %x\n", pathname, context, mode);

	if (strncmp(pathname, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return mkfifo(pathname, context, mode);
}

int dbg_symlink(const char *oldpath, const char *newpath, struct pierrefs_sb_info *context) {
	pr_info("dbg_symlink: %s, %s, %p\n", oldpath, newpath, context);

	if (strncmp(newpath, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return symlink(oldpath, newpath, context);
}

int dbg_link(const char *oldpath, const char *newpath, struct pierrefs_sb_info *context) {
	pr_info("dbg_link: %s, %s, %p\n", oldpath, newpath, context);

	if (strncmp(newpath, context->read_only_branch, context->ro_len) == 0) {
		pr_err("Attempted to write on RO branch!\n");
		return -EINVAL;
	}

	return link(oldpath, newpath, context);
}
#endif
