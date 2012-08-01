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

int can_access(const char *path, const char *real_path, int mode) {
	struct kstat stbuf;
	int err;

	pr_info("can_access: %s, %s, %x\n", path, real_path, mode);

	/* Get file attributes */
	err = get_file_attr_worker(path, real_path, &stbuf);
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

int can_remove(const char *path, const char *real_path) {
	char parent_path[PATH_MAX];

	/* Find parent directory */
	char *parent = strrchr(real_path, '/');

	pr_info("can_remove: %s, %s\n", path, real_path);

	/* Caller wants to remove /! */
	if (parent == real_path) {
		return -EACCES;
	}

	strncpy(parent_path, real_path, parent - real_path);
	parent_path[parent - real_path] = '\0';

	/* Caller must be able to write in parent dir */
	return can_access(path, parent_path, MAY_WRITE);
}

int can_traverse(const char *path) {
	char short_path[PATH_MAX];
	char long_path[PATH_MAX];
	int err;
	char *last, *old_directory, *directory;

	pr_info("can_traverse: %s\n", path);

	/* Prepare strings */
	snprintf(short_path, PATH_MAX, "%c", '/');
	if (snprintf(long_path, PATH_MAX, "%s/", get_context()->read_only_branch) > PATH_MAX) {
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
		err = can_access(short_path, long_path, MAY_EXEC);
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

int find_file(const char *path, char *real_path, char flags) {
	int err;
	struct kstat kstbuf;
	char tmp_path[PATH_MAX];
	char wh_path[PATH_MAX];

	pr_info("find_file: %s, %p, %x\n", path, real_path, flags);

	/* Do not check flags validity
	 * Caller can only be internal
	 * So it must be trusted
	 */
	if (!is_flag_set(flags, MUST_READ_ONLY)) {
		/* First try RW branch (higher priority) */
		if (make_rw_path(path, real_path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		push_root();
		err = vfs_lstat(real_path, &kstbuf);
		pop_root();
		if (err < 0) {
			if (is_flag_set(flags, MUST_READ_WRITE)) {
				return err;
			}
		}
		else {
			/* Check for access */
			err = can_traverse(path);
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

		push_root();
		err = vfs_lstat(tmp_path, &kstbuf);
		pop_root();
		if (err < 0) {
			/* If file does not exist, even in RO, fail */
			return err;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, wh_path);
			if (err < 0) {
				return err;
			}
		}

		/* Check for access */
		err = can_traverse(path);
		if (err < 0) {
			return err;
		}

		err = create_copyup(path, tmp_path, real_path);
		if (err == 0) {
			return READ_WRITE_COPYUP;
		}
	}
	else {
		/* It was not found on RW, try RO */
		if (make_ro_path(path, real_path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		push_root();
		err = vfs_lstat(real_path, &kstbuf);
		pop_root();
		if (err < 0) {
			return err;
		}

		if (!is_flag_set(flags, IGNORE_WHITEOUT)) {
			/* Check whether it was deleted */
			err = find_whiteout(path, wh_path);
			if (err == 0) {
				return -ENOENT;
			}
		}

		/* Check for access */
		err = can_traverse(path);
		if (err < 0) {
			return err;
		}

		return READ_ONLY;
	}

	/* It was not found at all */
	return err;
}

/* Adapted from nfs_path function */
int get_full_path(const struct inode *inode, const struct dentry *dentry, char *real_path) {
	char tmp_path[PATH_MAX];
	char *end = tmp_path+sizeof(tmp_path);
	int namelen = 0, buflen = PATH_MAX;

	pr_info("get_full_path: %p, %p, %p\n", inode, dentry, real_path);

	/* If we don't have any dentry, then, let's find one */
	if (!dentry) {
		/* FIXME: For the moment only~~ */
		/* assert(inode->i_nlink == 0); */

		if (inode->i_dentry.next) {
			dentry = list_entry(inode->i_dentry.next, struct dentry, d_alias);
		}
	}

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
	buflen -= sizeof(char);
	if (buflen < 0)
		goto Elong;
	end -= namelen;
	memcpy(end, "/", namelen);

	/* Copy back name */
	memcpy(real_path, end, buflen);
	return buflen;

Elong_unlock:
	spin_unlock(&dcache_lock);
Elong:
	return -ENAMETOOLONG;
}

struct dentry * get_path_dentry(const char *pathname, int flag) {
	int err;
	struct dentry *dentry;
	struct nameidata nd;

	pr_info("get_path_dentry: %s, %x\n", pathname, flag);

	push_root();
	err = __user_walk(pathname, flag, &nd);
	pop_root();
	if (err) {
		return ERR_PTR(err);
	}

	dentry = nd.dentry;
	dget(dentry);
	path_release(&nd);

	return dentry;
}

int get_relative_path(const struct inode *inode, const struct dentry *dentry, char *path, int is_ours) {
	int len;
	char real_path[PATH_MAX];
	struct pierrefs_sb_info *sb_info;

	pr_info("get_relative_path: %p, %p, %p, %d\n", inode, dentry, path, is_ours);

	/* First, get full path */
	len = get_full_path(inode, dentry, real_path);
	if (len < 0) {
		return len;
	}

	/* If those structures are owned by PierreFS, there's no
	 * need to skip the branch part
	 */
	if (is_ours) {
		return 0;
	}

	/* Get branches info */
	sb_info = get_context();

	/* Check if it's on RO */
	if (strncmp(sb_info->read_only_branch, real_path, sb_info->ro_len) == 0) {
		memcpy(path, real_path + 1 + sb_info->ro_len, len - 1 - sb_info->ro_len);
		return 0;
	}

	/* Check if it's on RW */
	if (strncmp(sb_info->read_write_branch, real_path, sb_info->rw_len) == 0) {
		memcpy(path, real_path + 1 + sb_info->rw_len, len - 1 - sb_info->rw_len);
		return 0;
	}

	return -EINVAL;
}

int get_relative_path_for_file(const struct inode *dir, const struct dentry *dentry, char *path, int is_ours) {
	int err;
	size_t len;

	pr_info("get_relative_path_for_file: %p, %p, %p, %d\n", dir, dentry, path, is_ours);

	/* First get path of the directory */
	err = get_relative_path(dir, 0, path, is_ours);
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

/* Imported for Linux kernel */
long mkdir(const char *pathname, int mode) {
	int error = 0;
	char * tmp;
	struct dentry *dentry;
	struct nameidata nd;

	pr_info("mkdir: %s, %x\n", pathname, mode);

	tmp = getname(pathname);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	push_root();
	error = path_lookup(tmp, LOOKUP_PARENT, &nd);
	pop_root();
	if (error)
		goto out;
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
out:
	putname(tmp);

	return error;
}

/* Imported from Linux kernel */
long mknod(const char *pathname, int mode, unsigned dev) {
	int error = 0;
	char * tmp;
	struct dentry * dentry;
	struct nameidata nd;

	pr_info("mknod: %s, %x, %u\n", pathname, mode, dev);

	if (S_ISDIR(mode))
		return -EPERM;
	tmp = getname(pathname);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	push_root();
	error = path_lookup(tmp, LOOKUP_PARENT, &nd);
	pop_root();
	if (error)
		goto out;
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
out:
	putname(tmp);

	return error;
}

int mkfifo(const char *pathname, int mode) {
	pr_info("mkfifo: %s, %x\n", pathname, mode);

	/* Ensure FIFO mode is set */
	mode |= S_IFIFO;

	/* Call mknod */
	return mknod(pathname, mode, 0);
}

/* Imported from Linux kernel */
long symlink(const char *oldname, const char *newname) {
	int error = 0;
	char * from;
	char * to;

	pr_info("symlink: %s, %s\n", oldname, newname);

	from = getname(oldname);
	if(IS_ERR(from))
		return PTR_ERR(from);
	to = getname(newname);
	error = PTR_ERR(to);
	if (!IS_ERR(to)) {
		struct dentry *dentry;
		struct nameidata nd;

		push_root();
		error = path_lookup(to, LOOKUP_PARENT, &nd);
		pop_root();
		if (error)
			goto out;
		push_root();
		dentry = lookup_create(&nd, 0);
		pop_root();
		error = PTR_ERR(dentry);
		if (!IS_ERR(dentry)) {
			push_root();
			error = vfs_symlink(nd.dentry->d_inode, dentry, from, S_IALLUGO);
			pop_root();
			dput(dentry);
		}
		mutex_unlock(&nd.dentry->d_inode->i_mutex);
		path_release(&nd);
out:
		putname(to);
	}
	putname(from);
	return error;
}

/* Imported from Linux kernel - simplified */
long link(const char *oldname, const char *newname) {
	struct dentry *new_dentry;
	struct nameidata nd, old_nd;
	int error;
	char * to;

	pr_info("link: %s, %s\n", oldname, newname);

	to = getname(newname);
	if (IS_ERR(to))
		return PTR_ERR(to);

	push_root();
	error = __user_walk(oldname, 0, &old_nd);
	pop_root();
	if (error)
		goto exit;
	push_root();
	error = path_lookup(to, LOOKUP_PARENT, &nd);
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
exit:
	putname(to);

	return error;
}

/* Imported from Linux kernel */
long readlink(const char *path, char *buf, int bufsiz) {
	struct inode *inode;
	struct nameidata nd;
	int error;

	pr_info("readlink: %s, %p, %d\n", path, buf, bufsiz);

	if (bufsiz <= 0)
		return -EINVAL;

	push_root();
	error = __user_walk(path, 0, &nd);
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

struct file* dbg_open(const char *pathname, int flags) {
	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, 0);
}

struct file* dbg_open_2(const char *pathname, int flags, mode_t mode) {
	if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
		if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	return filp_open(pathname, flags, mode);
}

struct file* dbg_creat(const char *pathname, mode_t mode) {
	if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return ERR_PTR(-EINVAL);
	}

	return filp_creat(pathname, mode);
}

int dbg_mkdir(const char *pathname, mode_t mode) {
	if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return -EINVAL;
	}

	return mkdir(pathname, mode);
}

int dbg_mknod(const char *pathname, mode_t mode, dev_t dev) {
	if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return -EINVAL;
	}

	return mknod(pathname, mode, dev);
}

int dbg_mkfifo(const char *pathname, mode_t mode) {
	if (strncmp(pathname, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return -EINVAL;
	}

	return mkfifo(pathname, mode);
}

int dbg_symlink(const char *oldpath, const char *newpath) {
	if (strncmp(newpath, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return -EINVAL;
	}

	return symlink(oldpath, newpath);
}

int dbg_link(const char *oldpath, const char *newpath) {
	if (strncmp(newpath, get_context()->read_only_branch, get_context()->ro_len) == 0) {
		return -EINVAL;
	}

	return link(oldpath, newpath);
}
