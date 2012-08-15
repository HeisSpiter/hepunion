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

static int pierrefs_closedir(struct inode *inode, struct file *filp) {
	struct readdir_file *entry;
	struct opendir_context *ctx = (struct opendir_context *)filp->private_data;

	pr_info("pierrefs_closedir: %p, %p\n", inode, filp);

	/* First, clean all the lists */
	while (!list_empty(&ctx->whiteouts_head)) {
		entry = list_entry(ctx->whiteouts_head.next, struct readdir_file, files_entry);
		list_del(&entry->files_entry);
		kfree(entry);
	}

	while (!list_empty(&ctx->files_head)) {
		entry = list_entry(ctx->files_head.next, struct readdir_file, files_entry);
		list_del(&entry->files_entry);
		kfree(entry);
	}

	/* Then, release the context itself */
	kfree(ctx);

	return 0;
}

static int pierrefs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nameidata) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;
	struct file* filp;
	struct iattr attr;
	struct inode *inode;

	pr_info("pierrefs_create: %p, %p, %x, %p\n", dir, dentry, mode, nameidata);

	will_use_buffers(context);
	validate_inode(dir);

	/* Try to find the file first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(path, real_path, context, 0);
	if (err >= 0) {
		release_buffers(context);
		return -EEXIST;
	}

	/* Once we are here, we know that the file does not exist
	 * And that we can create it (thanks to lookup)
	 */
	/* Create path if needed */
	err = find_path(path, real_path, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Be paranoid, check access */
	err = can_create(path, real_path, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Open the file */
	filp = creat_worker(real_path, context, mode);
	if (IS_ERR(filp)) {
		release_buffers(context);
		return PTR_ERR(filp);
	}

	/* Set its correct owner in case of creation */
	attr.ia_uid = current->uid;
	attr.ia_gid = current->gid;
	attr.ia_valid = ATTR_UID | ATTR_GID;

	push_root();
	err = notify_change(filp->f_dentry, &attr);
	filp_close(filp, 0);
	pop_root();

	if (err < 0) {
		dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(dentry)) {
			release_buffers(context);
			return err;
		}

		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);

		release_buffers(context);
		return err;
	}

	/* Now we're done, create the inode */
	inode = new_inode(dir->i_sb);
	if (!inode) {
		dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(dentry)) {
			release_buffers(context);
			return err;
		}

		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);

		release_buffers(context);
		return -ENOMEM;
	}

	/* And fill it in */
	dir->i_nlink++;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mtime = CURRENT_TIME;
	inode->i_atime = CURRENT_TIME;
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blkbits = 0;
	inode->i_op = &pierrefs_iops;
	inode->i_fop = &pierrefs_fops;
	inode->i_mode = mode;
	inode->i_nlink = 1;
	inode->i_ino = name_to_ino(path);
#ifdef _DEBUG_
	inode->i_private = (void *)PIERREFS_MAGIC;
#endif
	insert_inode_hash(inode); 

	d_instantiate(dentry, inode);
	mark_inode_dirty(dir);
	mark_inode_dirty(inode);

	/* Remove whiteout if any */
	unlink_whiteout(path, context);

	release_buffers(context);
	return 0;
}

static int pierrefs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstbuf) {
	int err;
	struct pierrefs_sb_info *context = get_context_d(dentry);
	char *path = context->global1;

	pr_info("pierrefs_getattr: %p, %p, %p\n", mnt, dentry, kstbuf);

	will_use_buffers(context);

	/* Get path */
	err = get_relative_path(0, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Call worker */
	err = get_file_attr(path, context, kstbuf);
	if (err >= 0) {
		/* Set our inode number */
		kstbuf->ino = dentry->d_inode->i_ino;
	}

	release_buffers(context);
	return err;
}

static int pierrefs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) {
	int err, origin;
	struct pierrefs_sb_info *context = get_context_d(old_dentry);
	char *from = context->global1;
	char *to = context->global2;
	char real_from[PATH_MAX];
	char real_to[PATH_MAX];

	pr_info("pierrefs_link: %p, %p, %p\n", old_dentry, dir, dentry);

	will_use_buffers(context);
	validate_inode(dir);

	/* First, find file */
	err = get_relative_path(0, old_dentry, context, from, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	origin = find_file(from, real_from, context, 0);
	if (origin < 0) {
		release_buffers(context);
		return origin;
	}

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, context, to, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, context, 0);
	if (err >= 0) {
		release_buffers(context);
		return -EEXIST;
	}

	/* Check access */
	err = can_create(to, real_to, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Create path if needed */
	err = find_path(to, real_to, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	if (origin == READ_ONLY) {
		/* Here, fallback to a symlink */
		err = symlink_worker(real_from, real_to, context);
		if (err < 0) {
			release_buffers(context);
			return err;
		}
	}
	else {
		/* Get RW name */
		if (make_rw_path(to, real_to) > PATH_MAX) {
			release_buffers(context);
			return -ENAMETOOLONG;
		}

		err = link_worker(real_from, real_to, context);
		if (err < 0) {
			release_buffers(context);
			return err;
		}
	}

	/* Remove possible whiteout */
	unlink_whiteout(to, context);

	release_buffers(context);
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
	struct read_inode_context * ctx;
	size_t namelen;
	unsigned long ino;

	pr_info("pierrefs_lookup: %p, %p, %p\n", dir, dentry, nameidata);

	will_use_buffers(context);
	validate_inode(dir);

	/* First get path of the file */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return ERR_PTR(err);
	}

	pr_info("Looking for: %s\n", path);

	/* Set our operations before we continue */
	dentry->d_op = &pierrefs_dops;

	/* Now, look for the file */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		if (err == -ENOENT) {
			pr_info("Null inode\n");
			d_add(dentry, inode);
			release_buffers(context);
			return NULL;
		} else {
			pr_info("Err: %d\n", err);
			release_buffers(context);
			return ERR_PTR(err);
		}
	}

	/* We've got it!
	 * Prepare a read_inode context for further read
	 */
	namelen = strlen(path); 
	ino = name_to_ino(path);
	ctx = kmalloc(sizeof(struct read_inode_context) + (namelen + 1) * sizeof(path[0]), GFP_KERNEL);
	ctx->ino = ino;
	memcpy(ctx->name, path, namelen * sizeof(path[0]));
	ctx->name[namelen] = 0;
	list_add(&ctx->read_inode_entry, &context->read_inode_head);

	/* Get inode */
	inode = iget(dir->i_sb, ino);
	if (!inode) {
		inode = ERR_PTR(-EACCES);
	} else {
		/* Set our inode */
		d_add(dentry, inode);
	}

	/* Release the context, whatever happened
	 * If inode was new, read_inode has been called and the context used
	 * otherwise it was just useless
	 */
	list_del(&ctx->read_inode_entry);
	kfree(ctx);

	release_buffers(context);
	return NULL;
}

static int pierrefs_mkdir(struct inode *dir, struct dentry *dentry, int mode) {
	int err;
	struct inode *inode;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_mkdir: %p, %p, %x\n", dir, dentry, mode);

	will_use_buffers(context);
	validate_inode(dir);

	/* Try to find the directory first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(path, real_path, context, 0);
	if (err >= 0) {
		release_buffers(context);
		return -EEXIST;
	}

	/* Get full path for destination */
	if (make_rw_path(path, real_path) > PATH_MAX) {
		release_buffers(context);
		return -ENAMETOOLONG;
	}

	/* Check access */
	err = can_create(path, real_path, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Now, create/reuse arborescence */
	err = find_path(path, real_path, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Ensure we have good mode */
	mode |= S_IFDIR;

	/* Just create dir now */
	err = mkdir_worker(real_path, context, mode);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Hide contents */
	err = hide_directory_contents(path, context);
	if (err < 0) {
		dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(dentry)) {
			release_buffers(context);
			return err;
		}

		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);

		release_buffers(context);
		return err;
	}

	/* Now we're done, create the inode */
	inode = new_inode(dir->i_sb);
	if (!inode) {
		dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(dentry)) {
			release_buffers(context);
			return err;
		}

		push_root();
		vfs_unlink(dentry->d_inode, dentry);
		pop_root();
		dput(dentry);

		release_buffers(context);
		return -ENOMEM;
	}

	/* And fill it in */
	dir->i_nlink++;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mtime = CURRENT_TIME;
	inode->i_atime = CURRENT_TIME;
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_blkbits = 0;
	inode->i_op = &pierrefs_dir_iops;
	inode->i_fop = &pierrefs_dir_fops;
	inode->i_mode = mode;
	inode->i_nlink = 1;
	inode->i_ino = name_to_ino(path);
#ifdef _DEBUG_
	inode->i_private = (void *)PIERREFS_MAGIC;
#endif
	insert_inode_hash(inode); 

	d_instantiate(dentry, inode);
	mark_inode_dirty(dir);
	mark_inode_dirty(inode);

	/* Remove possible .wh. */
	unlink_whiteout(path, context);

	release_buffers(context);
	return 0;
}

static int pierrefs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_mknod: %p, %p, %x, %x\n", dir, dentry, mode, rdev);

	will_use_buffers(context);
	validate_inode(dir);

	/* Try to find the node first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(path, real_path, context, 0);
	if (err >= 0) {
		release_buffers(context);
		return -EEXIST;
	}

	/* Now, create/reuse arborescence */
	err = find_path(path, real_path, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Just create file now */
	if (S_ISFIFO(mode)) {
		err = mkfifo_worker(real_path, context, mode);
		if (err < 0) {
			release_buffers(context);
			return err;
		}
	}
	else {
		err = mknod_worker(real_path, context, mode, rdev);
		if (err < 0) {
			release_buffers(context);
			return err;
		}
	}

	/* Remove possible whiteout */
	unlink_whiteout(path, context);

	release_buffers(context);
	return 0;
}

static int pierrefs_open(struct inode *inode, struct file *file) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(inode);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_open: %p, %p\n", inode, file);

	will_use_buffers(context);
	validate_inode(inode);

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
		release_buffers(context);
		return err;
	}

	/* Really open the file.
	 * The associated file object on real file system is stored
	 * as private data of the PierreFS file object. This is used
	 * to maintain data consistency and to forward requests on
	 * the file to the lower file system.
	 */
	file->private_data = open_worker_2(real_path, context, file->f_flags, file->f_mode);
	if (IS_ERR(file->private_data)) {
		err = PTR_ERR(file->private_data);
		file->private_data = 0;
		release_buffers(context);
		return err;
	}

	release_buffers(context);
	return 0;
}

static int pierrefs_opendir(struct inode *inode, struct file *file) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(inode);
	char *path = context->global1;
	char *real_path = context->global2;
	struct opendir_context *ctx;
	char ro_path[PATH_MAX];
	char rw_path[PATH_MAX];
	size_t ro_len = 0;
	size_t rw_len = 0;

	pr_info("pierrefs_opendir: %p, %p\n", inode, file);

	will_use_buffers(context);
	validate_inode(inode);

	/* Don't check for flags here, if we are down here
	 * the user is allowed to read/write the dir, the
	 * dir was created if required (and allowed).
	 * Here, the only operation required is to open the
	 * dir on the underlying file system
	 */

	/* Get our directory path */
	err = get_relative_path(inode, file->f_dentry, context, path, 1);

	/* Get real directory path */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	if (find_file(path, rw_path, context, MUST_READ_WRITE) >= 0) {
		rw_len = strlen(rw_path);
	}

	if (find_file(path, ro_path, context, MUST_READ_ONLY) >= 0) {
		ro_len = strlen(ro_path);
	}

	/* Allocate readdir context */
	ctx = kmalloc(sizeof(struct opendir_context) + rw_len + ro_len + 2 * sizeof(char), GFP_KERNEL);
	if (!ctx) {
		release_buffers(context);
		return -ENOMEM;
	}

	/* Copy strings - RO first */
	if (ro_len) {
		ctx->ro_len = ro_len;
		ctx->ro_off = sizeof(struct opendir_context);

		strncpy((char *)(ctx->ro_off + (unsigned long)ctx), ro_path, ro_len);
		*((char *)(ctx->ro_off + ro_len + (unsigned long)ctx)) = '\0';
	}
	else {
		ctx->ro_len =
		ctx->ro_off = 0;
	}

	/* Then RW */
	if (rw_len) {
		ctx->rw_len = rw_len;
		ctx->rw_off = sizeof(struct opendir_context) + ro_len;
		/* Don't forget \0 */
		if (ro_len) {
			ctx->rw_off += sizeof(char);
		}

		strncpy((char *)(ctx->rw_off + (unsigned long)ctx), rw_path, rw_len);
		*((char *)(ctx->rw_off + rw_len + (unsigned long)ctx)) = '\0';
	}
	else {
		ctx->rw_len =
		ctx->rw_off = 0;
	}

	/* Keep inode */
	ctx->context = context;

	/* Init list heads */
	INIT_LIST_HEAD(&ctx->files_head);
	INIT_LIST_HEAD(&ctx->whiteouts_head);

	file->private_data = ctx;

	release_buffers(context);
	return 0;
}

static int pierrefs_permission(struct inode *inode, int mask, struct nameidata *nd) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(inode);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_permission: %p, %x, %p\n", inode, mask, nd);

	will_use_buffers(context);
	validate_inode(inode);

	/* Get path */
	err = get_relative_path(inode, (nd ? nd->dentry : NULL), context, path, 1);
	if (err) {
		release_buffers(context);
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And call worker */
	err = can_access(path, real_path, context, mask);

	release_buffers(context);
	return err;
}

static void pierrefs_read_inode(struct inode *inode) {
	int err;
	struct kstat kstbuf;
	struct list_head *entry;
	struct read_inode_context *ctx;
	struct pierrefs_sb_info *context = get_context_i(inode);

	pr_info("pierrefs_read_inode: %p\n", inode);

	/* Get path */
	entry = context->read_inode_head.next;
	while (entry != &context->read_inode_head) {
		ctx = list_entry(entry, struct read_inode_context, read_inode_entry);
		if (ctx->ino == inode->i_ino) {
			break;
		}

		entry = entry->next;
	}

	/* Quit if no context found */
	if (entry == &context->read_inode_head) {
		pr_info("Context not found for: %lu\n", inode->i_ino);
		return;
	}

	pr_info("Reading inode: %s\n", ctx->name);

	/* Call worker */
	err = get_file_attr(ctx->name, context, &kstbuf);
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

	/* Set operations */
	if (inode->i_mode & S_IFDIR) {
		inode->i_op = &pierrefs_dir_iops;
		inode->i_fop = &pierrefs_dir_fops;
	} else {
		inode->i_op = &pierrefs_iops;
		inode->i_fop = &pierrefs_fops;
	}

#ifdef _DEBUG_
	inode->i_private = (void *)PIERREFS_MAGIC;
#endif
}

static int read_rw_branch(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	struct readdir_file *entry;
	struct opendir_context *ctx = (struct opendir_context *)buf;
	struct pierrefs_sb_info *context = ctx->context;
	char complete_path[PATH_MAX];
	char *path;
	size_t len;

	pr_info("read_rw_branch: %p, %s, %d, %llx, %llx, %d\n", buf, name, namlen, offset, ino, d_type);

#if 0
	/* Skip special */
	if (is_special(name, namlen)) {
		return 0;
	}
#endif

	/* Ignore metadata */
	if (is_me(name, namlen)) {
		return 0;
	}

	/* Handle whiteouts */
	if (is_whiteout(name, namlen)) {
		/* Just work if there's a RO branch */
		if (ctx->ro_len) {
			/* Fix name len. Don't take .wh. into account
			 * It will be removed
			 * Prefix isn't mandatory, since context makes it obvious
			 */
			namlen -= 4; /* strlen(".wh."); */

			/* Allocate a list big enough to contain data and null terminated name */
			entry = kmalloc(sizeof(struct readdir_file) + (namlen + 1) * sizeof(char), GFP_KERNEL);
			if (!entry) {
				return -ENOMEM;
			}

			/* Add it to list */
			list_add(&entry->files_entry, &ctx->whiteouts_head);

			/* Fill in data */
			entry->d_reclen = namlen;
			strncpy(entry->d_name, name + 4, namlen);
			entry->d_name[namlen] = '\0';
		}
	}
	else {
		/* This is a normal entry
		 * Just add it to the list
		 */
		entry = kmalloc(sizeof(struct readdir_file) + namlen + sizeof(char), GFP_KERNEL);
		if (!entry) {
			return -ENOMEM;
		}

		/* Add it to list */
		list_add(&entry->files_entry, &ctx->files_head);

		/* Fill in data */
		entry->d_reclen = namlen;
		entry->type = d_type;
		strncpy(entry->d_name, name, namlen);
		entry->d_name[namlen] = '\0';

		complete_path[0] = '\0';

		/* Get its ino */
		if (ctx->ro_len) {
			path = (char *)(ctx->ro_off + (unsigned long)ctx);
			len = ctx->ro_len - 1 - context->ro_len;
			if (strncmp(context->read_only_branch, path, context->ro_len) == 0) {
				memcpy(complete_path, path + 1 + context->ro_len, len);
				complete_path[len] = '\0';
			}
		}

		if (complete_path[0] == 0 && ctx->rw_len) {
			path = (char *)(ctx->rw_off + (unsigned long)ctx);
			len = ctx->rw_len - 1 - context->rw_len;
			if (strncmp(context->read_write_branch, path, context->rw_len) == 0) {
				memcpy(complete_path, path + 1 + context->rw_len, ctx->rw_len - 1 - context->rw_len);
				complete_path[len] = '\0';
			}
		}

		if (complete_path[0] != 0) {
			entry->ino = name_to_ino(complete_path);
		} else {
			entry->ino = 0;
		}
	}

	return 0;
}

static int read_ro_branch(void *buf, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_type) {
	struct opendir_context *ctx = (struct opendir_context *)buf;
	struct pierrefs_sb_info *context = ctx->context;
	char complete_path[PATH_MAX];
	char *path;
	size_t len;
	struct readdir_file *entry;
	struct list_head *lentry;

	pr_info("read_ro_branch: %p, %s, %d, %llx, %llx, %d\n", buf, name, namlen, offset, ino, d_type);

#if 0
	/* Skip special */
	if (is_special(name, namlen)) {
		return 0;
	}
#endif

	/* Check if there is any matching whiteout */
	lentry = ctx->whiteouts_head.next;
	while (lentry != &ctx->whiteouts_head) {
		entry = list_entry(lentry, struct readdir_file, files_entry);
		if (namlen == entry->d_reclen &&
			strncmp(name, entry->d_name, namlen) == 0) {
			/* There's a whiteout, forget the entry */
			return 0;
		}

		lentry = lentry->next;
	}

	/* Check if it matches a RW entry */
	lentry = ctx->files_head.next;
	while (lentry != &ctx->files_head) {
		entry = list_entry(lentry, struct readdir_file, files_entry);
		if (namlen == entry->d_reclen &&
			strncmp(name, entry->d_name, namlen) == 0) {
			/* There's a RW entry, forget the entry */
			return 0;
		}

		lentry = lentry->next;
	}

	/* Finally, add the entry in list */
	entry = kmalloc(sizeof(struct readdir_file) + namlen + sizeof(char), GFP_KERNEL);
	if (!entry) {
		return -ENOMEM;
	}

	/* Add it to list */
	list_add(&entry->files_entry, &ctx->files_head);

	/* Fill in data */
	entry->d_reclen = namlen;
	entry->type = d_type;
	strncpy(entry->d_name, name, namlen);
	entry->d_name[namlen] = '\0';

	complete_path[0] = '\0';

	/* Get its ino */
	if (ctx->ro_len) {
		path = (char *)(ctx->ro_off + (unsigned long)ctx);
		len = ctx->ro_len - 1 - context->ro_len;
		if (strncmp(context->read_only_branch, path, context->ro_len) == 0) {
			memcpy(complete_path, path + 1 + context->ro_len, ctx->ro_len - 1 - context->ro_len);
			complete_path[len] = '\0';
		}
	}

	if (complete_path[0] == 0 && ctx->rw_len) {
		path = (char *)(ctx->rw_off + (unsigned long)ctx);
		len = ctx->rw_len - 1 - context->rw_len;
		if (strncmp(context->read_write_branch, path, context->rw_len) == 0) {
			memcpy(complete_path, path + 1 + context->rw_len, ctx->rw_len - 1 - context->rw_len);
			complete_path[len] = '\0'; 
		}
	}

	if (complete_path[0] != 0) {
		entry->ino = name_to_ino(complete_path);
	} else {
		entry->ino = 0;
	}

	return 0;
}

static int pierrefs_readdir(struct file *filp, void *dirent, filldir_t filldir) {
	int err = 0;
	int i = 0;
	struct readdir_file *entry;
	struct list_head *lentry;
	struct opendir_context *ctx = (struct opendir_context *)filp->private_data;
#ifdef _DEBUG_
	struct pierrefs_sb_info *context = ctx->context;
#endif

	pr_info("pierrefs_readdir: %p, %p, %p\n", filp, dirent, filldir);

	if (list_empty(&ctx->files_head)) {
		/* Here fun begins.... */
		struct file *rw_dir;
		struct file *ro_dir;

		/* Check if there is an associated RW dir */
		if (ctx->rw_len) {
			char *rw_dir_path = (char *)(ctx->rw_off + (unsigned long)ctx);

			/* Start browsing RW dir */
			rw_dir = open_worker(rw_dir_path, context, O_RDONLY);
			if (IS_ERR(rw_dir)) {
				err = PTR_ERR(rw_dir);
				goto cleanup;
			}

			err = vfs_readdir(rw_dir, read_rw_branch, ctx);
			filp_close(rw_dir, 0);

			if (err < 0) {
				goto cleanup;
			}
		}

		/* Work on RO branch */
		if (ctx->ro_len) {
			char *ro_dir_path = (char *)(ctx->ro_off + (unsigned long)ctx);

			/* Start browsing RO dir */
			ro_dir = open_worker(ro_dir_path, context, O_RDONLY);
			if (IS_ERR(ro_dir)) {
				err = PTR_ERR(ro_dir);
				goto cleanup;
			}

			err = vfs_readdir(ro_dir, read_ro_branch, ctx);
			filp_close(ro_dir, 0);

			if (err < 0) {
				goto cleanup;
			}
		}

		/* Now we have files list, clean whiteouts */
		while (!list_empty(&ctx->whiteouts_head)) {
			entry = list_entry(ctx->whiteouts_head.next, struct readdir_file, files_entry);
			list_del(&entry->files_entry);
			kfree(entry);
		}
	}

	/* Reset error */
	err = 0;

	pr_info("Looking for entry: %lld\n", filp->f_pos);

	/* Try to find the requested entry now */
	lentry = ctx->files_head.next;
	while (lentry != &ctx->files_head) {
		/* Found the entry - return it */
		if (i == filp->f_pos) {
			entry = list_entry(lentry, struct readdir_file, files_entry);
			pr_info("Found: %s\n", entry->d_name);
			filldir(dirent, entry->d_name, entry->d_reclen, i, entry->ino, entry->type);
			/* Update position */
			++filp->f_pos;
			break;
		}

		++i;
		lentry = lentry->next;
	}

cleanup:
	/* There was an error, clean everything */
	if (err < 0) {
		while (!list_empty(&ctx->whiteouts_head)) {
			entry = list_entry(ctx->whiteouts_head.next, struct readdir_file, files_entry);
			list_del(&entry->files_entry);
			kfree(entry);
		}

		while (!list_empty(&ctx->files_head)) {
			entry = list_entry(ctx->files_head.next, struct readdir_file, files_entry);
			list_del(&entry->files_entry);
			kfree(entry);
		}
	}

	return err;
}

static int pierrefs_revalidate(struct dentry *dentry, struct nameidata *nd) {
	pr_info("pierrefs_revalidate: %p, %p\n", dentry, nd);

	if (dentry->d_inode == NULL) {
		return 0;
	}

	return 1;
}

static int pierrefs_setattr(struct dentry *dentry, struct iattr *attr) {
	int err;
	struct dentry *real_dentry;
	struct pierrefs_sb_info *context = get_context_d(dentry);
	char *path = context->global1;
	char *real_path = context->global2;

	pr_info("pierrefs_setattr: %p, %p\n", dentry, attr);

	will_use_buffers(context);

	/* Get path */
	err = get_relative_path(0, dentry, context, path, 1);
	if (err) {
		release_buffers(context);
		return err;
	}

	/* Get file */
	err = find_file(path, real_path, context, 0);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	if (err == READ_WRITE || err == READ_WRITE_COPYUP) {
		/* Get dentry for the file to update */
		real_dentry = get_path_dentry(real_path, context, LOOKUP_REVAL);
		if (IS_ERR(real_dentry)) {
			release_buffers(context);
			return PTR_ERR(real_dentry);
		}

		/* Just update file attributes */
		push_root();
		err = notify_change(real_dentry, attr);
		pop_root();
		dput(real_dentry);

		release_buffers(context);
		return err;
    }

	/* Update me
	 * Don't clear flags, set_me_worker will do
	 * So, only call the worker
	 */
	err = set_me_worker(path, real_path, attr, context);

	release_buffers(context);
	return err;
}

static int pierrefs_symlink(struct inode *dir, struct dentry *dentry, const char *symname) {
	/* Create the link on the RW branch */
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *to = context->global1;
	char *real_to = context->global2;

	pr_info("pierrefs_symlink: %p, %p, %s\n", dir, dentry, symname);

	will_use_buffers(context);
	validate_inode(dir);

	/* Find destination */
	err = get_relative_path_for_file(dir, dentry, context, to, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* And ensure it doesn't exist */
	err = find_file(to, real_to, context, 0);
	if (err >= 0) {
		release_buffers(context);
		return -EEXIST;
	}

	/* Get full path for destination */
	if (make_rw_path(to, real_to) > PATH_MAX) {
		release_buffers(context);
		return -ENAMETOOLONG;
	}

	/* Check access */
	err = can_create(to, real_to, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Create path if needed */
	err = find_path(to, real_to, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Now it's sure the link does not exist, create it */
	err = symlink_worker(symname, real_to, context);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Remove possible whiteout */
	unlink_whiteout(to, context);

	release_buffers(context);
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

static int pierrefs_unlink(struct inode *dir, struct dentry *dentry) {
	int err;
	struct pierrefs_sb_info *context = get_context_i(dir);
	char *path = context->global1;
	char *real_path = context->global2;
	struct kstat kstbuf;
	char me_path[PATH_MAX];
	char wh_path[PATH_MAX];

	pr_info("pierrefs_unlink: %p, %p\n", dir, dentry);

	will_use_buffers(context);
	validate_inode(dir);

	/* Try to find the file first */
	err = get_relative_path_for_file(dir, dentry, context, path, 1);
	if (err < 0) {
		release_buffers(context);
		return err;
	}

	/* Then, find file */
	err = find_file(path, real_path, context, 0);
	switch (err) {
		int has_me = 0;
		/* On RW, just remove it */
		case READ_WRITE_COPYUP: /* Can't happen */
		case READ_WRITE:
			unlink_rw_file(path, real_path, context, 0);
			break;

		/* On RO, create a whiteout */
		case READ_ONLY:
			/* Check if user can unlink file */
			if (!can_remove(path, real_path, context)) {
				break;
			}

			/* Get me first */
			if (find_me(path, context, me_path, &kstbuf) >= 0) {
				has_me = 1;
				/* Delete it */
				if (unlink(me_path, context)) {
					break;
				}
			}

			/* Now, create whiteout */
			err = create_whiteout(path, wh_path, context);
			if (err < 0 && has_me) {
				create_me(me_path, &kstbuf, context);
			}
			break;

		default:
			/* Nothing to do */
			break;
	}

	return err;
}

struct inode_operations pierrefs_iops = {
	.getattr	= pierrefs_getattr,
	.permission	= pierrefs_permission,
#if 0
	.readlink	= generic_readlink, /* dentry will already point on the right file */
#endif
	.setattr	= pierrefs_setattr,
	.unlink		= pierrefs_unlink,
};

struct inode_operations pierrefs_dir_iops = {
	.create		= pierrefs_create,
	.getattr	= pierrefs_getattr,
	.link		= pierrefs_link,
	.lookup		= pierrefs_lookup,
	.mkdir		= pierrefs_mkdir,
	.mknod		= pierrefs_mknod,
	.permission	= pierrefs_permission,
	.setattr	= pierrefs_setattr,
	.symlink	= pierrefs_symlink,
};

struct super_operations pierrefs_sops = {
	.read_inode	= pierrefs_read_inode,
	.statfs		= pierrefs_statfs,
};

struct dentry_operations pierrefs_dops = {
	.d_revalidate	= pierrefs_revalidate,
};

struct file_operations pierrefs_fops = {
	.llseek		= pierrefs_llseek,
	.open		= pierrefs_open,
};

struct file_operations pierrefs_dir_fops = {
	.open		= pierrefs_opendir,
	.readdir	= pierrefs_readdir,
	.release	= pierrefs_closedir,
};
