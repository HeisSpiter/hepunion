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
	.fs_flags	= FS_REVAL_DOT,
};

static void get_branches(struct super_block *sb, void *raw_data) {
}

static int pierrefs_read_super(struct super_block *sb, void *raw_data,
			       int silent) {
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
    get_branches(sb, raw_data);

	return -EINVAL;
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

static int __init init_pierrefs_fs(void) {
	return register_filesystem(&pierrefs_fs_type);
}

static void __exit exit_pierrefs_fs(void) {
	unregister_filesystem(&pierrefs_fs_type);
}

module_init(init_pierrefs_fs);
module_exit(exit_pierrefs_fs);
