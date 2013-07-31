/**
 * \file hepunion.h
 * \brief Global header file included in all HEPunion files
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 21-Nov-2011
 * \copyright GNU General Public License - GPL
 * \todo Implementing caching
 *
 * HEPunion file system intend to provide a file systems unioning
 * solution that comes with several specifications:
 * - Copy-on-write
 * - Low redundancy in files
 * - Data and metadata separation
 */

#ifndef __HEPUNION_H__
#define __HEPUNION_H__

#ifdef __KERNEL__

#include <linux/hepunion_type.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/uaccess.h>
#include <linux/security.h>
#include <linux/cred.h>
#include "hash.h"
#include "recursivemutex.h"

#define _DEBUG_

struct read_inode_context {
	/**
	 * Entry in the read_inode list
	 */
	struct list_head read_inode_entry;
	/**
	 * Inode number
	 */
	unsigned long ino;
	/**
	 * Associated path. It is null terminated
	 * \warning This is variable length structure
	 */
	char name[1];
};

struct hepunion_sb_info {
	/**
	 * Contains the full path of the RW branch
	 * \warning It is not \ terminated
	 */
	char *read_write_branch;
	/**
	 * Size of the RW branch path
	 */
	size_t rw_len;
	/**
	 * Contains the full path of the RO branch
	 * \warning It is not \ terminated
	 */
	char *read_only_branch;
	/**
	 * Size of the RO branch path
	 */
	size_t ro_len;
	/**
	 * Contains the UID when switched to root
	 */
	uid_t uid;
	/**
	 * Contains the GID when switched to root
	 */
	gid_t gid;
	/**
	 * Spin lock to protect uid/gid access
	 * \warning Only use the push_root() and pop_root()
	 */
	recursive_mutex_t id_lock;
	/**
	 * Strings big enough to contain a path
	 */
	char global1[PATH_MAX];
	char global2[PATH_MAX];
#ifdef _DEBUG_
	/**
	 * Set to 1 if global1 and global2 are being used
	 * by a function.
	 * It is used to detect contexts override
	 */
	int buffers_in_use;
#endif
	/**
	 * Head for the read_inode contexts list used during
	 * lookup
	 */
	struct list_head read_inode_head;
};

struct readdir_context {
	/**
	 * Read-only path string that may be used in callback function
	 */
	const char *ro_path;
	/**
	 * Any path (likely read-write) string that may be used in callback function
	 */
	const char *path;
	/**
     * Context with which vfs_readdir was called
	 */
	struct hepunion_sb_info *context;
};

/**
 * \brief Structure defining a directory entry during unioning
 *
 * This structure, used into a single linked list entry, contains
 * one entry of a directory browsing.
 * You first set the attributes of the name of the entry. And then,
 * you put the name of entry.
 * \warning This is a non-fixed sized structure
 */
struct readdir_file {
	/**
	 * Pointer to the next list entry.
	 * If there's none, set to NULL
	 */
	struct list_head files_entry;
	/**
	 * Length of the string containing the file name
	 */
	unsigned short d_reclen;
	/**
	 * Inode number of the entry
	 */
	unsigned long ino;
	/**
	 * Tye of the entry
	 */
	unsigned type;
	/**
	 * String containing the file name. It's allocated with the structure
	 */
	char d_name[1];
};

/**
 * \brief Structure defining a directory browsing context
 *
 * It optionaly contains the full path of the RO branch to browse
 * and/or the full path of the RW to browse.
 * First, you set strings length and offset (both to 0 if not set)
 * and after those data, you put the strings.
 * \warning This is a non-fixed sized structure
 */
struct opendir_context {
	/**
	 * Context used for opendir
	 */
	struct hepunion_sb_info *context;
	/**
	 * Head of the list containing all the files to be returned
	 */
	struct list_head files_head;
	/**
	 * Head of the list containing all the whiteouts found during unioning
	 */
	struct list_head whiteouts_head;
	/**
	 * Length of the string containing the RO branch directory.
	 * Set it to 0 if there is no RO branch directory
	 * \note You shouldn't count NULL char in it
	 */
	size_t ro_len;
	/**
	 * This is the offset at which the RO brach directory string
	 * starts AFTER the address of the structure
	 * Set it to 0 if there is no RO branch directory
	 */
	size_t ro_off;
	/**
	 * Length of the string containing the RW branch directory
	 * Set it to 0 if there is no RW branch directory
	 * \note You shouldn't count NULL char in it
	 */
	size_t rw_len;
	/**
	 * This is the offset at which the RW brach directory string
	 * starts AFTER the address of the structure
	 * Set it to 0 if there is no RW branch directory
	 */
	size_t rw_off;
};

/**
 * \brief Enumeration defining all the possible returns of the find_file() function
 * \sa find_file
 *
 * Those are used to describe where the find_file() function found a file (if ever it
 * found one).
 */
typedef enum _types {
	/**
	 * The file was found on the RO branch
	 */
	READ_ONLY = 0,
	/**
	 * The file was found on the RW branch
	 */
	READ_WRITE = 1,
	/**
	 * The file was found on the RO branch, and a copyup has been created
	 */
	READ_WRITE_COPYUP = 2
} types;

typedef enum _specials {
	ME = 0,
	WH = 1
} specials;

extern struct inode_operations hepunion_iops;
extern struct inode_operations hepunion_dir_iops;
extern struct super_operations hepunion_sops;
extern struct dentry_operations hepunion_dops;
extern struct file_operations hepunion_fops;
extern struct file_operations hepunion_dir_fops;

/**
 * Rights mask used to handle shifting with st_mode rights definition.
 * It allows you to skip a set of right to go to the next one.
 * First, others. One shift (on the left), group. Second shift, user
 * \sa can_access
 */
#define RIGHTS_MASK	0x3

/**
 * Flag to pass to find_file() function. It indicates that if the file was
 * found RO a copyup has to be done and its path returned
 * \sa find_file
 */
#define CREATE_COPYUP	0x1
/**
 * Flag to pass to find_file() function. It indicates that the file has to
 * already exist on the RW branch. If it doesn't, the function will fail
 * \sa find_file
 */
#define MUST_READ_WRITE	0x2
/**
 * Flag to pass to find_file() function. The function will only check the
 * RO branch to find the function. If it doesn't exist there, the function
 * will fail (even if it could have existed on RW branch)
 * \sa find_file
 */
#define MUST_READ_ONLY	0x4
/**
 * Flag to pass to find_file() function. It indicates that the file that
 * the function will return might not exist regarding union method
 * \sa find_file
 */
#define IGNORE_WHITEOUT	0x8

/**
 * Flag to pass to the set_me() function. It indicates that the st_uid and
 * st_gid fields will be used to define both user & group of the file
 * \sa set_me
 */
#define OWNER	0x1
/**
 * Flag to pass to the set_me() function. It indicates that the st_mode
 * field will be used to define the mode of a file
 * \sa set_me
 */
#define MODE	0x2
/**
 * Flag to pass to the set_me() function. It indicates that the st_atime and
 * st_mtime fields will be used to define both last access time and modification
 * time
 * \sa set_me
 */
#define TIME	0x4

/**
 * Defines the maximum size that will be used for buffers to manipulate files
 */
#define MAXSIZE 4096

/**
  * Defines the seed key for the inode numbers
 */
#define HEPUNION_SEED 0x9F5109F5109F510BLLU

/**
 * Mask that defines all the modes of a file that can be changed using the
 * metadata mechanism
 */
#define VALID_MODES_MASK (S_ISUID | S_ISGID | S_ISVTX |	\
			  S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR |	\
			  S_IRWXG | S_IRGRP | S_IWGRP | S_IXGRP |	\
			  S_IRWXO | S_IROTH | S_IWOTH | S_IXOTH)

/**
 * Clear the opening/creating flags that could be sent to the open
 * function
 * This only allows rights bits
 * \param[in]	f	The flags to clear
 * \return	The cleared flags
 */
#define clear_mode_flags(f) f &= VALID_MODES_MASK
/**
 * Check if in a set of flags, another set of flags is set
 * \param[in]	s	The set of flags in which to check
 * \param[in]	f	The seeked flags
 * \return	1 if all seeked flags are set, 0 otherwise
 */
#define is_flag_set(s, f) ((s & f) == f)

/**
 * Check if the given directory entry is a metadata file against its name
 * \param[in]	e	dir_entry structure pointer
 * \return	1 if that's a metadata file, 0 otherwise
 * \warning	You MUST have defined d_reclen field the structure before using this macro
 * \note	Here, 4 is the length of ".me."
 */
#define is_me(n, l)					\
	(l > 4 && n[0] == '.' &&		\
	 n[1] == 'm' &&	n[2] == 'e' &&	\
	 n[3] == '.')
/**
 * Check if the given directory entry is a whiteout file against its name
 * \param[in]	e	dir_entry structure pointer
 * \return	1 if that's a whiteout file, 0 otherwise
 * \warning	You MUST have defined d_reclen field the structure before using this macro
 * \note	Here, 4 is the length of ".wh."
 */
#define is_whiteout(n, l)			\
	(l > 4 && n[0] == '.' &&		\
	 n[1] == 'w' &&	n[2] == 'h' &&	\
	 n[3] == '.')
/**
 * Check if the given directory entry is a special file (. or ..)
 * \param[in]	e	dir_entry structure pointer
 * \return	1 if that's a special file, 0 otherwise
 * \warning	You MUST have defined d_reclen field the structure before using this macro
 */
#define is_special(n, l)		\
	((l == 1 && n[0] == '.') ||	\
	 (l == 2 &&	n[0] == '.' &&	\
	  n[1] == '.'))

/**
 * Get current context associated with dentry
 * \param[in]	d	dentry pointer
 * \return	It returns super block info structure (hepunion_sb_info)
 */
#define get_context_d(d) ((struct hepunion_sb_info *)d->d_sb->s_fs_info)
/**
 * Get current context associated with inode
 * \param[in]	i	inode pointer
 * \return	It returns super block info structure (hepunion_sb_info)
 */
#define get_context_i(i) ((struct hepunion_sb_info *)i->i_sb->s_fs_info)
/**
 * Generate the string matching the given path for a full RO path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RO path
 * \return	The number of caracters written to r
 */
#define make_ro_path(p, r) snprintf(r, PATH_MAX, "%s%s", context->read_only_branch, p)
/**
 * Generate the string matching the given path for a full RW path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RW path
 * \return	The number of caracters written to r
 */
#define make_rw_path(p, r) snprintf(r, PATH_MAX, "%s%s", context->read_write_branch, p)
/**
 * Switch the current context user and group to root to allow
 * modifications on child file systems
 */
#define pop_root()							\
	do {									\
		struct cred *new = prepare_creds();	\
		new->fsuid = context->uid;			\
		new->fsgid = context->gid;			\
		commit_creds(new);					\
	} while(0);								\
	recursive_mutex_unlock(&context->id_lock)
/**     
 * Switch the current context back to real user and real group
 */
#define push_root()							\
	recursive_mutex_lock(&context->id_lock);\
	context->uid = current_fsuid();			\
	context->gid = current_fsgid();			\
	do {									\
		struct cred *new = prepare_creds();	\
		new->fsuid = 0;						\
		new->fsgid = 0;						\
		commit_creds(new);					\
	} while(0)
/**
 * Switch the current data segment to disable buffers checking
 * To be used when calling a VFS function wanting an usermode
 * buffer
 */
#define call_usermode()	\
	oldfs = get_fs();	\
	set_fs(KERNEL_DS)
/**
 * Switch back to previous data segment, thanks to the stored value
 */
#define restore_kernelmode()	\
	set_fs(oldfs)
/**
 * Convert a name (relative path name) to an inode number
 * \param[in]	n	The name to translate
 * \return	The associated inode number
 */
#define name_to_ino(n) murmur_hash_64a(n, strlen(n) * sizeof(n[0]), HEPUNION_SEED)
/**
 * Kernel mode assertion
 * In case the expression is unverified, kernel panic
 * \param[in]	e	The expression that has to be true
 * \return Nothing
 */
#define assert(e)																		\
	if (!(e)) {																			\
		pr_crit("Assertion %s failed at line: %d, file: %s\n", #e, __LINE__, __FILE__);	\
		BUG_ON(!(e));																	\
	}

#define filp_creat(p, m) filp_open(p, O_CREAT | O_WRONLY | O_TRUNC, m)

#ifdef _DEBUG_
#define open_worker(p, c, f) dbg_open(p, c, f)
#define open_worker_2(p, c, f, m) dbg_open_2(p, c, f, m)
#define creat_worker(p, c, m) dbg_creat(p, c, m)
#define mkdir_worker(p, c, m) dbg_mkdir(p, c, m)
#define mknod_worker(p, c, m, d) dbg_mknod(p, c, m, d)
#define mkfifo_worker(p, c, m) dbg_mkfifo(p, c, m)
#define symlink_worker(o, n, c) dbg_symlink(o, n, c)
#define link_worker(o, n, c) dbg_link(o, n, c)

#define will_use_buffers(c)			\
	assert(c->buffers_in_use == 0);	\
	c->buffers_in_use = 1
#define release_buffers(c)			\
	assert(c->buffers_in_use == 1);	\
	c->buffers_in_use = 0
#define validate_inode(i)	\
	assert((unsigned long)i->i_private == HEPUNION_MAGIC)
#define validate_dentry(d)	\
	assert((unsigned long)d->d_fsdata == HEPUNION_MAGIC)

#else
#define open_worker(p, c, f) filp_open(p, f, 0)
#define open_worker_2(p, c, f, m) filp_open(p, f, m)
#define creat_worker(p, c, m) filp_creat(p, m)
#define mkdir_worker(p, c, m) mkdir(p, c, m)
#define mknod_worker(p, c, m, d) mknod(p, c, m, d)
#define mkfifo_worker(p, c, m) mkfifo(p, c, m)
#define symlink_worker(o, n, c) symlink(o, n, c)
#define link_worker(o, n, c) link(o, n, c)

#define will_use_buffers(c)
#define release_buffers(c)
#define validate_inode(i)
#define validate_dentry(d)

#endif

/* Functions in cow.c */
/**
 * Create a copyup for a file.
 * File, here, can describe everything, including directory
 * \param[in]	path	Relative path of the file to copy
 * \param[in]	ro_path	Full path of the file to copy
 * \param[out]	rw_path Full path of the copied file
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int create_copyup(const char *path, const char *ro_path, char *rw_path, struct hepunion_sb_info *context);
/**
 * Find a path that is available in RW.
 * If none exists, but RO path exists, then a copyup of the
 * path will be done
 * \param[in]	path		Relative path of the path to check
 * \param[out]	real_path	Optionnal full path available in RW
 * \param[in]	context		Calling context of the FS
 * \return	0 in case of a success, -err in case of error
 */
int find_path(const char *path, char *real_path, struct hepunion_sb_info *context);
/**
 * Delete a copyup but restore attributes of the file through a me if required
 * \param[in]	path		Relative path of the file
 * \param[in]	copyup_path	Complete path of the copyup
 * \param[in]	context		Calling context of the FS
 * \return	0 in case of a success, -err in case of error
 */
int unlink_copyup(const char *path, const char *copyup_path, struct hepunion_sb_info *context);

/* Functions in me.c */
/**
 * Create a metadata file from scrach only using path
 * and metadata.
 * \param[in]	me_path	Full path of the metadata file to create
 * \param[in]	kstbuf	Structure containing all the metadata to use
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -err in case of an error
 * \note	To set metadata of a file, use set_me() instead
 */
int create_me(const char *me_path, struct kstat *kstbuf, struct hepunion_sb_info *context);
/**
 * Find the metadata file associated with a file and query
 * its properties.
 * \param[in]	path	Relative path of the file to check
 * \param[in]	context	Calling context of the FS
 * \param[out]	me_path	Full path of the possible metadata file
 * \param[out]	kstbuf	Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 */
int find_me(const char *path, struct hepunion_sb_info *context, char *me_path, struct kstat *kstbuf);
/**
 * Query the unioned metadata of a file. This can include the read
 * of a metadata file.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	context		Calling context of the FS
 * \param[out]	kstbuf		Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 * \note	In case you already have full path, prefer using get_file_attr_worker()
 */
int get_file_attr(const char *path, struct hepunion_sb_info *context, struct kstat *kstbuf);
/**
 * Query the unioned metadata of a file. This can include the read
 * of a metadata file.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	real_path	Full path of the file to check
 * \param[in]	context		Calling context of the FS
 * \param[out]	kstbuf		Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 * \note	In case you don't have full path, use get_file_attr() that will find it for you
 */
int get_file_attr_worker(const char *path, const char *real_path, struct hepunion_sb_info *context, struct kstat *kstbuf);
/**
 * Set the metadata for a file, using a metadata file.
 * \param[in]	path		Relative path of the file to set
 * \param[in]	real_path	Full path of the file to set
 * \param[in]	kstbuf		Structure containing the metadata to set
 * \param[in]	context		Calling context of the FS
 * \param[in]	flags		ORed set of flags defining which metadata set (OWNER, MODE, TIME)
 * \return	0 in case of a success, -err in case of error
 * \warning	Never ever use that function on a RW file! This would lead to file system inconsistency
 * \note	In case you have an iattr struct, use set_me_worker() function
 * \todo	Would deserve a check for equality and .me. removal
 */
int set_me(const char *path, const char *real_path, struct kstat *kstbuf, struct hepunion_sb_info *context, int flags);
/**
 * Set the metadata for a file, using a metadata file.
 * \param[in]	path		Relative path of the file to set
 * \param[in]	real_path	Full path of the file to set
 * \param[in]	attr		Structure containing the metadata to set
 * \param[in]	context		Calling context of the FS
 * \return	0 in case of a success, -err in case of error
 * \warning	Never ever use that function on a RW file! This would lead to file system inconsistency
 * \note	If you have a kstat structure, you should use set_me() instead
 * \note	Only ATTR_UID, ATTR_GID, ATTR_ATIME, ATTR_MTIME, ATTR_MODE flags are supported
 * \todo	Would deserve a check for equality and .me. removal
 */
int set_me_worker(const char *path, const char *real_path, struct iattr *attr, struct hepunion_sb_info *context);

/* Functions in helpers.c */
/**
 * Check Read/Write/Execute permissions on a file for calling process.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	real_path	Full path of the file to check
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		ORed set of modes to check (R_OK, W_OK, X_OK)
 * \return	1 if calling process can access, -err in case of error
 * \note	This is checked against user, group, others permissions
 */
int can_access(const char *path, const char *real_path, struct hepunion_sb_info *context, int mode);
/**
 * Check permission for the calling process to create a file.
 * \param[in]	p	Relative path of the file to create
 * \param[in]	rp	Full path of the file to create
 * \param[in]	c	Calling context of the FS
 * \return	1 if calling process can create, -err in case of error
 * \note	This is just a wrapper to can_remove since required rights the same
 */
#define can_create(p, rp, c) can_remove(p, rp, c)
/**
 * Check permission for the calling process to remove a file.
 * \param[in]	path		Relative path of the file to remove
 * \param[in]	real_path	Full path of the file to remove
 * \param[in]	context		Calling context of the FS
 * \return	1 if calling process can remove, -err in case of error
 * \note	This is checked against user, group, others permissions for writing in parent directory
 */
int can_remove(const char *path, const char *real_path, struct hepunion_sb_info *context);
/**
 * Check permission for the calling process to go through a tree.
 * \param[in]	path	Relative path of the tree to traverse
 * \param[in]	context	Calling context of the FS
 * \return	1 if calling process can remove, 0 otherwise. errno is set
 * \note	This is checked against user, group, others permissions for execute in traverse directories
 */
int can_traverse(const char *path, struct hepunion_sb_info *context);
/**
 * Check whether the given path exists.
 * \param[in]	pathname	Pathname to lookup
 * \param[in]	context		Calling context of the FS
 * \param[in]	flag		Flag to use for looking up
 * \return dentry, or -err in case of error
 */
int check_exist(const char *pathname, struct hepunion_sb_info *context, int flag);
/**
 * Find a file either in RW or RO branch, taking into account whiteout files. It can copyup files if needed.
 * \param[in]	path		Relative path of the file to find
 * \param[out]	real_path	Full path of the file, if found
 * \param[in]	context		Calling context of the FS
 * \param[in]	flags		ORed set of flags defining where and how finding file (CREATE_COPYUP, MUST_READ_WRITE, MUST_READ_ONLY, IGNORE_WHITEOUT)
 * \return	-err in case of a failure, an unsigned integer describing where the file was found in case of a success
 * \note	Unless flags state the contrary, the RW branch is the first checked for the file
 * \note	In case you called the function with CREATE_COPYUP flag, and it succeded, then returned path is to RW file
 * \warning	There is absolutely no checks for flags consistency!
 */
int find_file(const char *path, char *real_path, struct hepunion_sb_info *context, char flags);
/**
 * Get the full path of a dentry (might it be on HEPunion or lower file system).
 * \param[in]	dentry		Dentry that refers to the file
 * \param[out]	real_path	The real path that has been found
 * \return Length written in real_path in case of a success, an error code otherwise
 */
int get_full_path_d(const struct dentry *dentry, char *real_path);
/**
 * Get the full path of an inode
 * \param[in]	inode		Inode that refers to the file
 * \param[out]	real_path	The real path that has been found
 * \return Length written in real_path in case of a success, an error code otherwise
 * \warning	The function will try to get the best dentry possible by browsing them all
 * \warning	It will compute the full path for each dentry and then, get its ino and compare with inode ino
 * \warning It will work best with HEPunion inode. For the rest, the last dentry will be used
 */
int get_full_path_i(const struct inode *inode, char *real_path);
/**
 * Get the relative path (to / of HEPunion) of the provided file.
 * \param[in]	inode	Inode that refers to the file
 * \param[in]	dentry	Dentry that refers to the file
 * \param[in]	context		Calling context of the FS
 * \param[out]	path	The relative path that has been found
 * \param[in]	is_ours	Set to 1, it means that dentry & inode are local to HEPunion
 * \return 0 in case of a success, an error code otherwise
 * \note	It is possible not to provide a dentry (but not recommended). An inode must be provided then
 * \note	It is possible not to provide an inode. A dentry must be provided then
 * \warning	If no dentry is provided, the function might fail to find the path to the file even if it is on the HEPunion volume
 */
int get_relative_path(const struct inode *inode, const struct dentry *dentry, const struct hepunion_sb_info *context, char *path, int is_ours);
/**
 * Get the relative path (to / of HEPunion) for the creation of the provided file.
 * \param[in]	dir		Inode that refers to the directory in which the file is to be created
 * \param[in]	dentry	Dentry that refers to the file to create in the directory
 * \param[in]	context	Calling context of the FS
 * \param[out]	path	The relative path that has been found
 * \param[in]	is_ours	Set to 1, it means that dentry & inode are local to HEPunion
 * \return 0 in case of a success, an error code otherwise
 * \warning	This fuction relies on get_relative_path() and its limitations apply here
 */
int get_relative_path_for_file(const struct inode *dir, const struct dentry *dentry, const struct hepunion_sb_info *context, char *path, int is_ours);
/**
 * Get the dentry representing the given path.
 * \param[in]	pathname	Pathname to lookup
 * \param[in]	context		Calling context of the FS
 * \param[in]	flag		Flag to use for opening
 * \return dentry, or -err in case of error
 */
struct dentry* get_path_dentry(const char *pathname, struct hepunion_sb_info *context, int flag);
/**
 * Given a HEPunion relative path transforms it to full path for either wh or me
 * \param[in]	path	The path to transform
 * \param[in]	type	Type of special file wanted (see specials)
 * \param[in]	context	Calling context of the FS
 * \param[out]	outpath	Buffer big enough (PATH_MAX) containing the special path
 * \return 0 in case of a success, an error code otherwise
 * \warning This function assumes that the path is PATH_MAX big
 */
int path_to_special(const char *path, specials type, const struct hepunion_sb_info *context, char *outpath);
/**
 * Implementation taken from Linux kernel (and simplified). It's here to allow creation
 * of a link using pathname.
 * \param[in]	oldname	Target of the link
 * \param[in]	newname	Link path
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -err otherwise
 */
long link(const char *oldname, const char *newname, struct hepunion_sb_info *context);
/**
 * Implementation taken from the Linux kernel (and simplitied). It's here to query files attributes
 * \param[in]	pathname	Path of which querying attributes
 * \param[in]	context		Calling context of the FS
 * \param[out]	stat		Queried attributes
 * \return	0 in case of a success, -err otherwise
 */
int lstat(const char *pathname, struct hepunion_sb_info *context, struct kstat *stat);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a directory
 * using pathname.
 * \param[in]	pathname	Directory to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the directory (see mkdir man page)
 * \return	0 in case of a success, -err otherwise
 */
long mkdir(const char *pathname, struct hepunion_sb_info *context, int mode);
/**
 * Wrapper for mknod that allows creation of a FIFO file using pathname.
 * \param[in]	pathname	FIFO file to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the file (see mkfifo man page)
 * \return 	0 in case of a success, -err otherwise
 */
int mkfifo(const char *pathname, struct hepunion_sb_info *context, int mode);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a special
 * file using pathname.
 * \param[in]	pathname	File to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the file (see mknod man page)
 * \param[in]	dev			Special device
 * \return	0 in case of a success, -err otherwise
 */
long mknod(const char *pathname, struct hepunion_sb_info *context, int mode, unsigned dev);
/**
 * Implementation taken from Linux kernel. It's here to allow link readding (seems like
 * that's not the job vfs_readdlink is doing).
 * \param[in]	path	Link to read
 * \param[out]	buf		Link target
 * \param[in]	context	Calling context of the FS
 * \param[in]	bufsiz	Output buffer size
 * \return	0 in case of a success, -err otherwise 
 */
long readlink(const char *path, char *buf, struct hepunion_sb_info *context, int bufsiz);
/**
 * Implementation taken from Linux kernel. It's here to allow deletion of a directory
 * using pathname.
 * \param[in]	pathname	Directory to delete
 * \param[in]	context		Calling context of the FS
 * \return	0 in case of a success, -err otherwise
 */
long rmdir(const char *pathname, struct hepunion_sb_info *context);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a symlink
 * using pathname.
 * \param[in]	oldname	Target of the symlink
 * \param[in]	newname	Symlink path
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -err otherwise
 */
long symlink(const char *oldname, const char *newname, struct hepunion_sb_info *context);
/**
 * Implementation taken from Linux kernel. It's here to allow deletion of a file
 * using pathname.
 * \param[in]	pathname	File to delete
 * \param[in]	context		Calling context of the FS
 * \return	0 in case of a success, -err otherwise
 */
long unlink(const char *pathname, struct hepunion_sb_info *context);
/**
 * Worker for debug purpose. It first checks opening mode and branch, and then call open.
 * This is used to catch bad calls to RO branch
 * \param[in]	pathname	File to open
 * \param[in]	context		Calling context of the FS
 * \param[in]	flags		Flags for file opening (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_open(const char *pathname, const struct hepunion_sb_info *context, int flags);
/**
 * Worker for debug purpose. It first checks opening mode and branch, and then call open.
 * This is used to catch bad calls to RO branch
 * \param[in]	pathname	File to open
 * \param[in]	context		Calling context of the FS
 * \param[in]	flags		Flags for file opening (see open man page)
 * \param[in]	mode		Mode to set to the file (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_open_2(const char *pathname, const struct hepunion_sb_info *context, int flags, mode_t mode);
/**
 * Worker for debug purpose. It checks if the file is to be created on the right branch
 * and then call creat
 * \param[in]	pathname	File to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the file (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_creat(const char *pathname, const struct hepunion_sb_info *context, mode_t mode);
/**
 * Worker for debug purpose. It checks if the directory is to be created on the right branch
 * and then call mkdir
 * \param[in]	pathname	Directory to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the directory (see mkdir man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mkdir(const char *pathname, struct hepunion_sb_info *context, mode_t mode);
/**
 * Worker for debug purpose. It checks if the special file is to be created on the right branch
 * and then call mknod
 * \param[in]	pathname	Special file to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the directory (see mknod man page)
 * \param[in]	dev			Attributes of the device (see mknod man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mknod(const char *pathname, struct hepunion_sb_info *context, mode_t mode, dev_t dev);
/**
 * Worker for debug purpose. It checks if the FIFO is to be created on the right branch
 * and then call mkfifo
 * \param[in]	pathname	FIFO to create
 * \param[in]	context		Calling context of the FS
 * \param[in]	mode		Mode to set to the directory (see mkfifo man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mkfifo(const char *pathname, struct hepunion_sb_info *context, mode_t mode);
/**
 * Worker for debug purpose. It checks if the symlink is to be created on the right branch
 * and then call symlink
 * \param[in]	oldpath	Target of the symlink
 * \param[in]	newpath	Symlink to create
 * \param[in]	context	Calling context of the FS
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_symlink(const char *oldpath, const char *newpath, struct hepunion_sb_info *context);
/**
 * Worker for debug purpose. It checks if the link is to be created on the right branch
 * and then call link
 * \param[in]	oldpath	Target of the link
 * \param[in]	newpath	Link to create
 * \param[in]	context	Calling context of the FS
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_link(const char *oldpath, const char *newpath, struct hepunion_sb_info *context);

/* Functions in wh.c */
/**
 * Delete a file on RO by creating a whiteout
 * \param[in]	path	The file to delete
 * \param[out]	wh_path	The whiteout path
 * \param[in]	context	Calling context of the FS
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int create_whiteout(const char *path, char *wh_path, struct hepunion_sb_info *context);
/**
 * Find the whiteout that might hide a file.
 * \param[in]	path	Relative path of the file to check
 * \param[in]	context	Calling context of the FS
 * \param[out]	wh_path	Full path of the found whiteout
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int find_whiteout(const char *path, struct hepunion_sb_info *context, char *wh_path);
/**
 * Create a whiteout for each file contained in a directory.
 * \param[in]	path	Relative path of the directory where to hide files
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -err otherwise.
 * \note	In case directory doesn't exist on RO branch, it's a success
 */
int hide_directory_contents(const char *path, struct hepunion_sb_info *context);
/**
 * Check, using unionion, whether is directory is empty. If regarding union it's, ensure it really is.
 * \param[in]	path	Relative path of the directory to check
 * \param[in]	ro_path	Full path on RO branch of the directory
 * \param[in]	rw_path	Optional, full path on RW branch of the directory
 * \param[in]	context	Calling context of the FS
 * \return	1 if empty, -err otherwise
 * \note	If you don't provide RW branch, no union will be done, it will just check for RO emptyness
 */
int is_empty_dir(const char *path, const char *ro_path, const char *rw_path, struct hepunion_sb_info *context);
/**
 * Unlink a file on RW branch, and whiteout possible file on RO branch.
 * \param[in]	path		Relative path of the file to unlink
 * \param[in]	rw_path		Full path of the file on RW branch to unlink
 * \param[in]	context		Calling context of the FS
 * \param[in]	has_ro_sure	Optional, set to 1 if you check that file exists on RO
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int unlink_rw_file(const char *path, const char *rw_path, struct hepunion_sb_info *context, char has_ro_sure);
/**
 * Unlink the whiteout hidding a file.
 * \param[in]	path	Relative path of the file to "restore"
 * \param[in]	context	Calling context of the FS
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int unlink_whiteout(const char *path, struct hepunion_sb_info *context);

#endif /* #ifdef __KERNEL__ */

#endif /* #ifndef __HEPUNION_H__ */
