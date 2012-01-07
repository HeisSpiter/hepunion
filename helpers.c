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

/* Adapted from nfs_path function */
int get_full_path(const struct inode *inode, const struct dentry *dentry, char *real_path)
{
	char tmp_path[MAX_PATH];
	char *end = tmp_path+sizeof(tmp_path);
	int namelen, buflen = MAX_PATH;

	/* FIXME: For the moment only~~ */
	assert(inode->i_nlink == 0);

	/* If we don't have any dentry, then, let's find one */
	if (!dentry) {
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
	memcpy(path, end, buflen);
	return buflen;

Elong_unlock:
	spin_unlock(&dcache_lock);
Elong:
	return -ENAMETOOLONG;
}

int get_relative_path(const struct inode *inode, const struct dentry *dentry, char *path) {
	int len;
	char real_path[MAX_PATH];

	/* First, get full path */
	len = get_full_path(inode, dentry, real_path);
	if (len < 0) {
		return len;
	}

	/* Check if it's on RO */
	if (strncmp(sb_info->read_only_branch, real_path, sb_info->ro_len) == 0) {
		memcpy(path, real_path + 1 + sb_info->ro_len, len - 1 - sb_info->ro_lean);
		return 0;
	}

	/* Check if it's on RW */
	if (strncmp(sb_info->read_write_branch, real_path, sb_info->rw_len) == 0) {
		memcpy(path, real_path + 1 + sb_info->rw_len, len - 1 - sb_info->rw_lean);
		return 0;
	}

	/* FIXME */
	return -1;
}

int can_access(const char *path, const char *real_path, int mode) {
	struct kstat stbuf;
	long euid, egid;
	int err;

	/* Get file attributes */
	err = get_file_attr_worker(path, real_path, &stbuf);
	if (err) {
		return err;
	}

	/* Get effective IDs */
	euid = sys_geteuid();
	egid = sys_getegid();

	/* If root user, allow almost everything */
	if (euid == 0) {
		if (mode & X_OK) {
			/* Root needs at least on X
			 * For rights details, see below
			 */
			if ((X_OK & (signed)stbuf.mode) ||
			    (X_OK << RIGHTS_MASK & (signed)stbuf.mode) ||
			    (X_OK << (RIGHTS_MASK * 2) & (signed)stbuf.mode)) {
				return 1;
			}
		}
		else {
			/* Root can read/write */
			return 1;
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
	if (euid == stbuf.uid) {
		mode <<= (RIGHTS_MASK * 2);
	}
	else if (egid == stbuf.gid) {
		mode <<= RIGHTS_MASK;
	}

	/* Now compare bit sets and return */
	if ((mode & (signed)stbuf.mode) == mode) {
		return 0;
	}
	else {
		return -EACCESS;
	}
}

