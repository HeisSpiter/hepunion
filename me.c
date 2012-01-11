/**
 * \file me.c
 * \brief Metadata (ME) support for the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 10-Dec-2011
 * \copyright GNU General Public License - GPL
 *
 * Metadata support in PierreFS file system is different that
 * in the other union file systems.
 *
 * Here, a clear difference is made between data and metadata.
 * This is why the concept of metadata support has been added
 * to this file system. It clearly mirors the idea of COW (
 * read cow.c header) but adapts it to the metadata of a file
 * or even a directory.
 *
 * That way, when an attempt to modify a file metadata is made
 * (owner, time or mode), instead of copying the whole file,
 * a copyup of its metadata is made in a separate file. This
 * contains no data, it just carries the metadata.
 *
 * In order to make this possible, deported metadata are made
 * of a file called .me.{original file} which is at the same
 * place than the original file, but on read-write branch.
 * This mechanism is of course not used when the file is on the
 * read-write branch.
 *
 * This also means that if a metadata file is first created,
 * and then a copyup is done, the metadata file will be deleted
 * and its contents merged to the copyup file.
 *
 * On the other hand, on copyup deletion when original file
 * still exists, a metadata file will be recreated.
 * .me. files don't appear during files listing (thanks to
 * unioning).
 *
 * Metadata handling present some particularities since we there
 * is a need to merge some metadata instead of just using metadata
 * file. Indeed, since you can change mode for every object on the
 * system, but metadata is always a simple file, there is a need
 * to merge mode than can be modified with metadata files and
 * non alterable metadata.
 */

int create_me(const char *me_path, struct kstat *kstbuf) {
	int err:
	struct file *fd;
	struct iattr attr;

	/* Get creation modes */
	mode_t mode = kstbuf->st_mode;
	clear_mode_flags(mode);

	/* Create file */
	fd = creat_worker(me_path, mode);
	if (IS_ERR(fd)) {
		return fd;
	}

	attr.ia_valid = ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_ATIME | ATTR_MTIME;
	attr.ia_mode = stbuf->st_mode;
	attr.ia_uid = stbuf->st_uid;
	attr.ia_gid = stbuf->st_gid;
	attr.ia_size = 0;
	attr.ia_atime = stbuf->st_atime;
	attr.ia_mtime = stbuf->st_mtime;
	attr.ia_ctime = stbuf->st_ctime;

	/* Set all the attributes */
	err = notify_change(fd->f_dentry->d_inode, &attr);
	filp_close(fd, 0);

	return err;
}

int find_me(const char *path, char *me_path, struct kstat *kstbuf) {
	/* Find name */
	char *tree_path = strrchr(path, '/');
	if (!tree_path) {
		return -EINVAL;
	}

	if (snprintf(me_path, PATH_MAX, "%s", sb_info->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Copy path (and /) */
	strncat(me_path, path, tree_path - path + 1);
	/* Append me */
	strcat(me_path, ".me.");
	/* Finalement copy name */
	strcat(me_path, tree_path + 1);

	/* Now, try to get properties */
	return vfs_lstat(me_path, stbuf);
}

int get_file_attr(const char *path, struct kstat *kstbuf) {
	char real_path[PATH_MAX];
	int err;

	/* First, find file */
	err = find_file(path, real_path, 0);
	if (err < 0) {
		return err;
	}

	/* Call worker */
	return get_file_attr_worker(path, real_path, stbuf);
}

int get_file_attr_worker(const char *path, const char *real_path, struct kstat *kstbuf) {
	int err;
	char me;
	struct kstat mest;
	char me_file[MAX_PATH];

	/* Look for a me file */
	me = (find_me(path, me_file, &mest) == 0);

	/* Get attributes */
	err = vfs_lstat(real_path, kstbuf);
	if (err < 0) {
		return err;
	}

	/* If me file was present, merge results */
	if (me) {
		kstbuf->uid = mest.uid;
		kstbuf->gid = mest.gid;
		kstbuf->atime = mest.atime;
		kstbuf->mtime = mest.mtime;
		kstbuf->ctime = mest.ctime;
		/* Now we need to merge modes */
		/* First clean .me. modes */
		mest.mode = clear_mode_flags(mest.mode);
		/* Then, clean real file modes */
		kstbuf->mode &= ~VALID_MODES_MASK;
		/* Finally, apply .me. modes */
		kstbuf->mode |= mest.mode;
	}
	return 0;
}

int set_me(const char *path, const char *real_path, struct kstat *kstbuf, int flags) {
	int err;
	char me;
	char me_path[PATH_MAX];
	struct kstat kstme;
	struct file *fd;
	struct iattr attr;

	/* Look for a me file */
	me = (find_me(path, me_path, &stme) == 0);

	if (!me) {
		/* Read real file info */
		err = vfs_lstat(real_path, &stme);
		if (err < 0) {
			return err;
		}

		/* Recreate path up to the .me. file */
		err = find_path(path, NULL);
		if (err < 0) {
			return err;
		}

		/* .me. does not exist, create it with appropriate mode */
		mode_t mode = (flags & MODE) ? stbuf->st_mode : stme.st_mode;
		clear_mode_flags(mode);

		fd = creat_worker(me_path, mode);
		if (IS_ERR(fd)) {
			return fd;
		}

		attr.ia_valid = ATTR_UID | ATTR_GID | ATTR_ATIME | ATTR_MTIME;

		/* Set its time */
		if (flags & TIME) {
			attr.ia_atime = stbuf->st_atime;
			attr.ia_mtime = stbuf->st_mtime;
		}
		else {
			attr.ia_atime = stme.st_atime;
			attr.ia_mtime = stme.st_mtime;
		}

		/* Set its owner */
		if (flags & OWNER) {
			attr.ia_uid = stbuf->st_uid;
			attr.ia_gid = stbuf->st_gid;
		}
		else {
			attr.ia_uid = stme.st_uid;
			attr.ia_gid = stme.st_gid;
		}

		err = notify_change(fd->f_dentry->d_inode, &attr);

		filp_close(fd, 0);
	}
	else {
		attr.ia_valid = 0;

		fd = filp_open(me_path, O_RDONLY, 0);
		if (IS_ERR(fd)) {
			return fd;
		}

		/* Only change required attributes */
		if (flags & MODE) {
			attr.ia_valid |= ATTR_MODE;
			attr.ia_mode = clear_mode_flags(stbuf->st_mode);
		}

		if (flags & TIME) {
			attr.ia_valid |= ATTR_ATIME | ATTR_MTIME;
			attr.ia_atime = stbuf->st_atime;
			attr.ia_mtime = stbuf->st_mtime;
		}

		if (flags & OWNER) {
			attr.ia_valid |= ATTR_UID | ATTR_GID;
			attr.ia_uid = stbuf->st_uid;
			attr.ia_gid = stbuf->st_gid;
		}

		if (attr.ia_valid) {
			err = notify_change(fd->f_dentry->d_inode, &attr);
		}
		else {
			err = 0;
		}

		filp_close(fd, 0);
	}

	return err;
}
