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

static int pierrefs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstbuf) {
	int err;
	struct pierrefs_sb_info *context = get_context_d(dentry);
	char *path = context->global1;

	pr_info("pierrefs_getattr: %p, %p, %p\n", mnt, dentry, kstbuf);

	/* Get path */
	err = get_relative_path(0, dentry, context, path, 1);
	if (err < 0) {
		return err;
	}

	/* Call worker */
	return get_file_attr(path, context, kstbuf);
}

static int pierrefs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) {
	int err, origin;
	struct pierrefs_sb_info *context = get_context_d(old_dentry);
	char *from = context->global1;
	char *to = context->global2;
	char real_from[PATH_MAX];
	char real_to[PATH_MAX];

	pr_info("pierrefs_link: %p, %p, %p\n", old_dentry, dir, dentry);

	/* First, find file */
	err = get_relative_path(0, old_dentry, context, from, 1);
	if (err < 0) {
		return err;
	}

	origin = find_file(from, real_from, context, 0);
	if (origin < 0) {
		return origin;
	}

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, context, to, 1);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, context, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	/* Check access */
	err = can_create(to, real_to, context);
	if (err < 0) {
		return err;
	}

	/* Create path if needed */
	err = find_path(to, real_to, context);
	if (err < 0) {
		return err;
	}

	if (origin == READ_ONLY) {
		/* Here, fallback to a symlink */
		err = symlink_worker(real_from, real_to, context);
		if (err < 0) {
			return err;
		}
	}
	else {
		/* Get RW name */
		if (make_rw_path(to, real_to) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		err = link_worker(real_from, real_to, context);
		if (err < 0) {
			return err;
		}
	}

	/* Remove possible whiteout */
	unlink_whiteout(to, context);

	return 0;
}

static loff_t pierrefs_llseek(struct file *file, loff_t offset, int origin) {
	int err = -EINVAL;
	struct file *real_file = (struct file *)file->private_data;

	pr_info("pierrefs_llseek: %p, %llx, %x\n", file, offset, origin);

	if (real_file->f_op->llseek) {
		err = real_file->f_op->llseek(real_file, offset, origin);
	}

	return err;
}

static struct dentry * pierrefs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nameidata) {
	/* We are looking for "dentry" in "dir" */
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;
	struct inode *inode = NULL;

	pr_info("pierrefs_lookup: %p, %p, %p\n", dir, dentry, nameidata);

	/* First get path of the file */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		return ERR_PTR(err);
	}

	/* Now, look for the file */
	err = find_file(path, real_path, context, 0);
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

static int pierrefs_mkdir(struct inode *dir, struct dentry *dentry, int mode) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_mkdir: %p, %p, %x\n", dir, dentry, mode);

	/* Try to find the directory first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(path, real_path, context, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	/* Get full path for destination */
	if (make_rw_path(path, real_path) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Check access */
	err = can_create(path, real_path, context);
	if (err < 0) {
		return err;
	}

	/* Now, create/reuse arborescence */
	err = find_path(path, real_path, context);
	if (err < 0) {
		return err;
	}

	/* Ensure we have good mode */
	mode |= S_IFDIR;

	/* Just create dir now */
	err = mkdir_worker(real_path, context, mode);
	if (err < 0) {
		return err;
	}

	/* Hide contents */
	err = hide_directory_contents(path, context);
	if (err < 0) {
		dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(dentry)) {
			return err;
		}

		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);

		return err;
	}

	/* Remove possible .wh. */
	unlink_whiteout(path, context);

	return 0;
}

static int pierrefs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_mknod: %p, %p, %x, %x\n", dir, dentry, mode, rdev);

	/* Try to find the node first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(path, real_path, context, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	/* Now, create/reuse arborescence */
	err = find_path(path, real_path, context);
	if (err < 0) {
		return err;
	}

	/* Just create file now */
	if (S_ISFIFO(mode)) {
		err = mkfifo_worker(real_path, context, mode);
		if (err < 0) {
			return err;
		}
	}
	else {
		err = mknod_worker(real_path, context, mode, rdev);
		if (err < 0) {
			return err;
		}
	}

	/* Remove possible whiteout */
	unlink_whiteout(path, context);

	return 0;
}

static int pierrefs_open(struct inode *inode, struct file *file) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(inode);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_open: %p, %p\n", inode, file);

	/* Don't check for flags here, if we are down here
	 * the user is allowed to read/write the file, the
	 * file was created if required (and allowed).
	 * Here, the only operation required is to open the
	 * file on the underlying file system
	 */

	/* Get our file path */
	err = get_relative_path(inode, file->f_dentry, context, path, 1);

	/* Get real file path */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		return err;
	}

	/* Really open the file.
	 * The associated file object on real file system is stored
	 * as private data of the PierreFS file object. This is used
	 * to maintain data consistency and to forward requests on
	 * the file to the lower file system.
	 */
	file->private_data = open_worker_2(real_path, file->f_flags, file->f_mode);
	if (IS_ERR(file->private_data)) {
		err = PTR_ERR(file->private_data);
		file->private_data = 0;
		return err;
	}

	return 0;
}

static int pierrefs_permission(struct inode *inode, int mask, struct nameidata *nd) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(inode);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_permission: %p, %x, %p\n", inode, mask, nd);

	/* Get path */
	err = get_relative_path(0, nd->dentry, context, path, 1);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		return err;
	}

	/* And call worker */
	return can_access(path, real_path, context, mask);
}

static void pierrefs_read_inode(struct inode *inode) {
	int err;
	char path[PATH_MAX];
	struct kstat kstbuf;
	struct pierrefs_sb_info *context = get_context_i(inode);

	pr_info("pierrefs_read_inode: %p\n", inode);

	/* Get path */
	err = get_relative_path(inode, 0, context, path, 1);
	if (err < 0) {
		pr_info("read_inode: %d\n", err);
		return;
	}

	/* Call worker */
	err = get_file_attr(path, context, &kstbuf);
	if (err < 0) {
		pr_info("read_inode: %d\n", err);
		return;
	}

	/* Set inode */
	inode->i_mode = kstbuf.mode;
	inode->i_atime = kstbuf.atime;
	inode->i_mtime = kstbuf.mtime;
	inode->i_ctime = kstbuf.ctime;
	inode->i_uid = kstbuf.uid;
	inode->i_gid = kstbuf.gid;
	inode->i_size = kstbuf.size;
	inode->i_nlink = kstbuf.nlink;
	inode->i_blocks = kstbuf.blocks;
	inode->i_blkbits = kstbuf.blksize;
}

static int pierrefs_setattr(struct dentry *dentry, struct iattr *attr) {
	int err;
	struct pierrefs_sb_info *context = get_context_d(dentry);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_setattr: %p, %p\n", dentry, attr);

	/* Get path */
	err = get_relative_path(0, dentry, context, path, 1);
	if (err) {
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, context, 0);
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
	return set_me_worker(path, real_path, attr, context);
}

static int pierrefs_symlink(struct inode *dir, struct dentry *dentry, const char *symname) {
	/* Create the link on the RW branch */
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *to = context->global1;
	char *real_to = context->global2;

	pr_info("pierrefs_symlink: %p, %p, %s\n", dir, dentry, symname);

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, context, to, 1);
	if (err < 0) {
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, context, 0);
	if (err >= 0) {
		return -EEXIST;
	}

	/* Get full path for destination */
	if (make_rw_path(to, real_to) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Check access */
	err = can_create(to, real_to, context);
	if (err < 0) {
		return err;
	}

	/* Create path if needed */
	err = find_path(to, real_to, context);
	if (err < 0) {
		return err;
	}

	/* Now it's sure the link does not exist, create it */
	err = symlink_worker(symname, real_to, context);
	if (err < 0) {
		return err;
	}

	/* Remove possible whiteout */
	unlink_whiteout(to, context);

	return 0;
}

static int pierrefs_statfs(struct dentry *dentry, struct kstatfs *buf) {
	struct super_block *sb = dentry->d_sb;
	struct pierrefs_sb_info * sb_info = sb->s_fs_info;
	struct file *filp;
	int err;

	pr_info("pierrefs_statfs: %p, %p\n", dentry, buf);

	memset(buf, 0, sizeof(*buf));

	/* First, get RO data */
	filp = filp_open(sb_info->read_only_branch, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		pr_err("Failed opening RO branch!\n");
		return PTR_ERR(filp);
	}

	err = vfs_statfs(filp->f_dentry, buf);
	filp_close(filp, 0);

	if (unlikely(err)) {
		return err;
	}

	/* Return them, but ensure we mark our stuff */
	buf->f_type = sb->s_magic;
	buf->f_fsid.val[0] = (u32)PIERREFS_SEED;
	buf->f_fsid.val[1] = (u32)(PIERREFS_SEED >> 32);

	return 0;
}

struct inode_operations pierrefs_iops = {
	.getattr	= pierrefs_getattr,
	.link		= pierrefs_link,
	.lookup		= pierrefs_lookup,
	.mkdir		= pierrefs_mkdir,
	.mknod		= pierrefs_mknod,
	.permission	= pierrefs_permission,
#if 0
	.readlink	= generic_readlink, /* dentry will already point on the right file */
#endif
	.setattr	= pierrefs_setattr,
	.symlink	= pierrefs_symlink,
};

struct super_operations pierrefs_sops = {
	.read_inode	= pierrefs_read_inode,
	.statfs		= pierrefs_statfs,
};

struct dentry_operations pierrefs_dops = {
};

struct file_operations pierrefs_fops = {
	.llseek		= pierrefs_llseek,
	.open		= pierrefs_open,
};

struct file_operations pierrefs_drops = {
};
