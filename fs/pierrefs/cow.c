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

int create_copyup(const char *path, const char *ro_path, char *rw_path) {
	return -1;
}

int find_path_worker(const char *path, char *real_path) {
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
	if (!last) {
		return -EINVAL;
	}

	memcpy(tree_path, path, last - path + 1);
	tree_path[last - path + 1] = '\0';
	tree_path_present = find_file(tree_path, real_tree_path, 0);
	/* Path should at least exist RO */
	if (tree_path_present < 0) {
		return -tree_path_present;
	}
	/* Path is already present, nothing to do */
	else if (tree_path_present == READ_WRITE) {
		/* Execpt filing in output buffer */
		if (snprintf(real_path, PATH_MAX, "%s%s", get_context()->read_write_branch, path) > PATH_MAX) {
			return -ENAMETOOLONG;
		}

		return 0;
	}

	/* Once here, recreating tree by COW is mandatory */
	if (snprintf(real_path, PATH_MAX, "%s/", get_context()->read_write_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* Also prepare for RO branch */
	if (snprintf(read_only, PATH_MAX, "%s/", get_context()->read_only_branch) > PATH_MAX) {
		return -ENAMETOOLONG;
	}

	/* If that's last (creating dir at root) */
	if (last == path) {
		if (snprintf(real_path, PATH_MAX, "%s/", get_context()->read_write_branch) > PATH_MAX) {
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
		if (vfs_lstat(real_path, &kstbuf) < 0) {
			/* Get previous dir properties */
			err = vfs_lstat(read_only, &kstbuf);
			if (err < 0) {
				return err;
			}

			/* Create directory */
			err = mkdir_worker(real_path, kstbuf.mode);
			if (err < 0) {
				return err;
			}

			/* Now, set all the previous attributes */
			dentry = get_path_dentry(real_path, LOOKUP_DIRECTORY);
			if (IS_ERR(dentry)) {
				/* FIXME: Should delete in case of failure */
				return PTR_ERR(dentry);
			}

			attr.ia_valid = ATTR_ATIME | ATTR_MTIME | ATTR_UID | ATTR_GID;
			attr.ia_atime = kstbuf.atime;
			attr.ia_mtime = kstbuf.mtime;
			attr.ia_uid = kstbuf.uid;
			attr.ia_gid = kstbuf.gid;

			err = notify_change(dentry->d_inode, &attr);

			if (err < 0) {
				vfs_unlink(dentry->d_inode, dentry);
				dput(dentry);
				return err;
			}

			dput(dentry);
		}

		/* Next iteration (skip /) */
		old_directory = directory;
		directory = strchr(directory + 1, '/');
	}

	/* Append name to create */
	strcat(real_path, last);

	/* It's over */
	return 0;
}

int find_path(const char *path, char *real_path) {
	if (real_path) {
		return find_path_worker(path, real_path);
	}
	else {
		char tmp_path[PATH_MAX];
		return find_path_worker(path, tmp_path);
	}
}
