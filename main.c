/**
 * \file main.c
 * \brief Entry point of the PierreFS file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 21-Nov-2011
 * \copyright GNU General Public License - GPL
 *
 * This is where arguments of the command line will be handle.
 * This includes branches discovery.
 * It fills in mount context in case of success.
 */

#include "pierrefs.h"

MODULE_AUTHOR("Pierre Schweitzer, CERN CH"
	      " (http://pierrefs.sourceforge.net)");
MODULE_DESCRIPTION("PierreFS " PIERREFS_VERSION
		   " (http://pierrefs.sourceforge.net)");
MODULE_LICENSE("GPL"); 

static struct file_system_type pierrefs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= PIERREFS_NAME,
	.mount		= pierrefs_mount,
	.kill_sb	= pierrefs_kill_sb,
	.fs_flags	= FS_REVAL_DOT,
};

static int make_path(const char *s, size_t n, char **path) {
    /* Zero output */
    *path = 0;

	/* First of all, look if it is relative path */
	if (s[0] != '/') {
        return -EINVAL;
	}

	/* Tailing has to be removed */
	if (s[n - 1] == '/') {
		n--;
	}

	/* Allocate one more ('\0') */
	*path = kmalloc((n + 1) * sizeof(char));
	if (*path) {
		memcpy(*path, s, n);
		*path[n] = '\0';
        return 0;
	}

    return -ENOMEM;
}

static int get_branches(struct super_block *sb, const char *arg) {
	int err;
	char *output;
	struct pierrefs_sb_info * sb_info = sb->s_fs_info;

	/* We are expecting 2 branches, separated by : */
	const char *part2 = strchr(arg, ':');
	if (!part2) {
		return -EINVAL;
	}

	/* Look for first branch type */
	const char *type = strchr(arg, '=');
	int forced_ro = 0;
	/* First branch has a type */
	if (type && type < part2) {
		/* Get branch name */
		err = make_path(arg, type - arg, &output);
		if (err || !ouput) {
			return err;
		}

		if (!strncmp(type + 1, "RW", 2)) {
			sb_info->read_write_branch = output;
		}
		else if (strncmp(type + 1, "RO", 2)) {
			return -EINVAL;
		}
		else {
			sb_info->read_only_branch = output;
			forced_ro = 1;
		}

		/* Get type for second branch */
		type = strchr(part2, '=');
	}
	/* It has no type => RO */
	else {
		/* Get branch name */
		err = make_path(arg, part2 - arg, &sb_info->read_only_branch);
		if (err || !sb_info->read_only_branch) {
			return err;
		}
	}

	/* Skip : */
	part2++;

	/* If second branch has a type */
	if (type) {
		/* Get branch name */
		err make_path(part2, type - part2, &output);
		if (err || !output) {
			return err;
		}

		if (!strncmp(type + 1, "RW", 2)) {
			if (sb_info->read_write_branch) {
				return -EINVAL;
			}
			sb_info->read_write_branch = output;
		}
		else if (strncmp(type + 1, "RO", 2)) {
			return -EINVAL;
		}
		else {
			if (forced_ro) {
				return -EINVAL;
			}
			sb_info->read_only_branch = output;
		}
	}
	else {
		/* It has no type, adapt given the situation */
		if (sb_info->read_write_branch) {
			err = make_path(part2, strlen(part2), &sb_info->read_only_branch);
			if (err || !sb_info->read_only_branch) {
				return err;
			}
		}
		else if (sb_info->read_only_branch) {
			err = make_path(part2, strlen(part2), &sb_info->read_write_branch);
			if (err || !sb_info->read_write_branch) {
				return err;
			}
		}
	}

	/* At this point, we should have the two branches set */
	if (!sb_info->read_only_branch || !sb_info->read_write_branch) {
		return -EINVAL;
	}

	/* Check for branches */
	struct file * filp = filp_open(sb_info->read_only_branch, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		return (filp == 0) ? -EINVAL : PTR_ERR(filp);
	}
	filp_close(filp, 0);

	filp = filp_open(sb_info->read_write_branch, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		return (filp == 0) ? -EINVAL : PTR_ERR(filp);
	}
	filp_close(filp, 0);

	return 0;
}

static int pierrefs_read_super(struct super_block *sb, void *raw_data,
			       int silent) {
	int err;

	/* Check for parameters */
	if (!raw_data) {
		return -EINVAL;
	}

	/* Allocate super block info structure */
	sb->s_fs_info = kzalloc(sizeof(struct pierrefs_sb_info), GFP_KERNEL);
	if (unlikely(!sb->s_fs_info)) {
		return -ENOMEM;
	}

	/* Get branches */
	err = get_branches(sb, raw_data);
	if (err) {
		if (sb->s_fs_info->read_only_branch) {
			kfree(sb->s_fs_info->read_only_branch);
		}
		if (sb->s_fs_info->read_write_branch) {
			kfree(sb->s_fs_info->read_write_branch);
		}
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
		return err;
	}

	return 0;
}

static struct dentry *pierrefs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *raw_data) {
	struct dentry *dentry = mount_nodev(fs_type, flags,
					    raw_data, pierrefs_read_super);
	if (IS_ERR_OR_NULL(dentry)) {
		return 0;
	}

	return dentry;
}

static void pierrefs_kill_sb(struct super_block *sb) {
	generic_shutdown_super(sb);
	if (sb->s_fs_info->read_only_branch) {
		kfree(sb->s_fs_info->read_only_branch);
	}
	if (sb->s_fs_info->read_write_branch) {
		kfree(sb->s_fs_info->read_write_branch);
	}
}

static int __init init_pierrefs_fs(void) {
	return register_filesystem(&pierrefs_fs_type);
}

static void __exit exit_pierrefs_fs(void) {
	unregister_filesystem(&pierrefs_fs_type);
}

module_init(init_pierrefs_fs);
module_exit(exit_pierrefs_fs);
