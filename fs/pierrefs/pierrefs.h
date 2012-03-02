/**
 * \file pierrefs.h
 * \brief Global header file included in all PierreFS files
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 21-Nov-2011
 * \copyright GNU General Public License - GPL
 * \todo Implementing caching
 *
 * PierreFS file system intend to provide a file systems unioning
 * solution that comes with several specifications:
 * - Copy-on-write
 * - Low redundancy in files
 * - Data and metadata separation
 */

#ifndef __PIERREFS_H__
#define __PIERREFS_H__

#ifdef __KERNEL__

#include <linux/pierrefs_type.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/security.h>

struct pierrefs_sb_info {
	/**
	 * Contains the full path of the RW branch
	 * \warning It is not \ terminated
	 */
	char *read_write_branch;
	size_t rw_len;
	/**
	 * Contains the full path of the RO branch
	 * \warning It is not \ terminated
	 */
	char *read_only_branch;
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
	spinlock_t id_lock;
	/**
	 * Strings big enough to contain a path
	 */
	char global1[PATH_MAX];
	char global2[PATH_MAX];
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

extern struct inode_operations pierrefs_iops;
extern struct super_operations pierrefs_sops;
extern struct dentry_operations pierrefs_dops;

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
 * Mask that defines all the modes of a file that can be changed using the
 * metadata mechanism
 */
#define VALID_MODES_MASK (S_ISUID | S_ISGID | S_ISVTX |			\
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
 * Get current context associated with mount point
 * \return	It returns super block info structure (pierrefs_sb_info)
 */
#define get_context() ((struct pierrefs_sb_info *)current->fs->rootmnt->mnt_sb->s_fs_info)
/**
 * Generate the string matching the given path for a full RO path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RO path
 * \return	The number of caracters written to r
 */
#define make_ro_path(p, r) snprintf(r, PATH_MAX, "%s%s", get_context()->read_only_branch, p)
/**
 * Generate the string matching the given path for a full RW path
 * \param[in]	p	The path for which full path is required
 * \param[out]	r	The string that will contain the full RW path
 * \return	The number of caracters written to r
 */
#define make_rw_path(p, r) snprintf(r, PATH_MAX, "%s%s", get_context()->read_write_branch, p)
/**
 * Switch the current context user and group to root to allow
 * modifications on child file systems
 */
#define pop_root()							\
	current->fsuid = get_context()->uid;	\
	current->fsgid = get_context()->gid;	\
	spin_unlock(&get_context()->id_lock)
/**
 * Switch the current context back to real user and real group
 */
#define push_root()							\
	spin_lock(&get_context()->id_lock);		\
	get_context()->uid = current->fsuid;	\
	get_context()->gid = current->fsgid;	\
	current->fsuid = 0;						\
	current->fsgid = 0

#define filp_creat(p, m) filp_open(p, O_CREAT | O_WRONLY | O_TRUNC, m)

#ifdef _DEBUG_
#define open_worker(p, f) dbg_open(p, f)
#define open_worker_2(p, f, m) dbg_open_2(p, f, m)
#define creat_worker(p, m) dbg_creat(p, m)
#define mkdir_worker(p, m) dbg_mkdir(p, m)
#define mknod_worker(p, m, d) dbg_mknod(p, m, d)
#define mkfifo_worker(p, m) dbg_mkfifo(p, m)
#define symlink_worker(o, n) dbg_symlink(o, n)
#define link_worker(o, n) dbg_link(o, n)
#else
#define open_worker(p, f) filp_open(p, f, 0)
#define open_worker_2(p, f, m) filp_open(p, f, m)
#define creat_worker(p, m) filp_creat(p, m)
#define mkdir_worker(p, m) mkdir(p, m)
#define mknod_worker(p, m, d) mknod(p, m, d)
#define mkfifo_worker(p, m) mkfifo(p, m)
#define symlink_worker(o, n) symlink(o, n)
#define link_worker(o, n) link(o, n)
#endif

/* Functions in cow.c */
/**
 * Create a copyup for a file.
 * File, here, can describe everything, including directory
 * \param[in]	path	Relative path of the file to copy
 * \param[in]	ro_path	Full path of the file to copy
 * \param[out]	rw_path Full path of the copied file
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int create_copyup(const char *path, const char *ro_path, char *rw_path);
/**
 * Find a path that is available in RW.
 * If none exists, but RO path exists, then a copyup of the
 * path will be done
 * \param[in]	path		Relative path of the path to check
 * \param[out]	real_path	Optionnal full path available in RW
 * \return	0 in case of a success, -err in case of error
 */
int find_path(const char *path, char *real_path);

/* Functions in me.c */
/**
 * Create a metadata file from scrach only using path
 * and metadata.
 * \param[in]	me_path	Full path of the metadata file to create
 * \param[in]	kstbuf	Structure containing all the metadata to use
 * \return	0 in case of a success, -err in case of an error
 * \note	To set metadata of a file, use set_me() instead
 */
int create_me(const char *me_path, struct kstat *kstbuf);
/**
 * Find the metadata file associated with a file and query
 * its properties.
 * \param[in]	path	Relative path of the file to check
 * \param[out]	me_path	Full path of the possible metadata file
 * \param[out]	kstbuf	Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 */
int find_me(const char *path, char *me_path, struct kstat *kstbuf);
/**
 * Query the unioned metadata of a file. This can include the read
 * of a metadata file.
 * \param[in]	path		Relative path of the file to check
 * \param[out]	kstbuf		Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 * \note	In case you already have full path, prefer using get_file_attr_worker()
 */
int get_file_attr(const char *path, struct kstat * kstbuf);
/**
 * Query the unioned metadata of a file. This can include the read
 * of a metadata file.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	real_path	Full path of the file to check
 * \param[out]	kstbuf		Structure containing extracted metadata in case of a success
 * \return	0 in case of a success, -err in case of error
 * \note	In case you don't have full path, use get_file_attr() that will find it for you
 */
int get_file_attr_worker(const char *path, const char *real_path, struct kstat *kstbuf);
/**
 * Set the metadata for a file, using a metadata file.
 * \param[in]	path		Relative path of the file to set
 * \param[in]	real_path	Full path of the file to set
 * \param[in]	kstbuf		Structure containing the metadata to set
 * \param[in]	flags		ORed set of flags defining which metadata set (OWNER, MODE, TIME)
 * \return	0 in case of a success, -err in case of error
 * \warning	Never ever use that function on a RW file! This would lead to file system inconsistency
 * \note	In case you have an iattr struct, use set_me_worker() function
 * \todo	Would deserve a check for equality and .me. removal
 */
int set_me(const char *path, const char *real_path, struct kstat *kstbuf, int flags);
/**
 * Set the metadata for a file, using a metadata file.
 * \param[in]	path		Relative path of the file to set
 * \param[in]	real_path	Full path of the file to set
 * \param[in]	attr		Structure containing the metadata to set
 * \return	0 in case of a success, -err in case of error
 * \warning	Never ever use that function on a RW file! This would lead to file system inconsistency
 * \note	If you have a kstat structure, you should use set_me() instead
 * \note	Only ATTR_UID, ATTR_GID, ATTR_ATIME, ATTR_MTIME, ATTR_MODE flags are supported
 * \todo	Would deserve a check for equality and .me. removal
 */
int set_me_worker(const char *path, const char *real_path, struct iattr *attr);

/* Functions in helpers.c */
/**
 * Check Read/Write/Execute permissions on a file for calling process.
 * \param[in]	path		Relative path of the file to check
 * \param[in]	real_path	Full path of the file to check
 * \param[in]	mode		ORed set of modes to check (R_OK, W_OK, X_OK)
 * \return	1 if calling process can access, -err in case of error
 * \note	This is checked against user, group, others permissions
 */
int can_access(const char *path, const char *real_path, int mode);
/**
 * Check permission for the calling process to create a file.
 * \param[in]	p	Relative path of the file to create
 * \param[in]	rp	Full path of the file to create
 * \return	1 if calling process can create, -err in case of error
 * \note	This is just a wrapper to can_remove since required rights the same
 */
#define can_create(p, rp) can_remove(p, rp)
/**
 * Check permission for the calling process to remove a file.
 * \param[in]	path		Relative path of the file to remove
 * \param[in]	real_path	Full path of the file to remove
 * \return	1 if calling process can remove, -err in case of error
 * \note	This is checked against user, group, others permissions for writing in parent directory
 */
int can_remove(const char *path, const char *real_path);
/**
 * Check permission for the calling process to go through a tree.
 * \param[in]	path	Relative path of the tree to traverse
 * \return	1 if calling process can remove, 0 otherwise. errno is set
 * \note	This is checked against user, group, others permissions for execute in traverse directories
 */
int can_traverse(const char *path);
/**
 * Find a file either in RW or RO branch, taking into account whiteout files. It can copyup files if needed.
 * \param[in]	path		Relative path of the file to find
 * \param[out]	real_path	Full path of the file, if found
 * \param[in]	flags		ORed set of flags defining where and how finding file (CREATE_COPYUP, MUST_READ_WRITE, MUST_READ_ONLY, IGNORE_WHITEOUT)
 * \return	-err in case of a failure, an unsigned integer describing where the file was found in case of a success
 * \note	Unless flags state the contrary, the RW branch is the first checked for the file
 * \note	In case you called the function with CREATE_COPYUP flag, and it succeded, then returned path is to RW file
 * \warning	There is absolutely no checks for flags consistency!
 */
int find_file(const char *path, char *real_path, char flags);
/**
 * Get the full path (ie, on the lower FS) of the provided file.
 * \param[in]	inode		Inode that refers to the file
 * \param[in]	dentry		Dentry that refers to the file
 * \param[out]	real_path	The real path that has been found
 * \return 0 in case of a success, an error code otherwise
 * \note	It is possible not to provide a dentry (but not recommended). An inode must be provided then
 * \note	It is possible not to provide an inode. A dentry must be provided then
 * \warning	If no dentry is provided, the function will return the path associated to the first dentry it finds
 */
int get_full_path(const struct inode *inode, const struct dentry *dentry, char *real_path);
/**
 * Get the relative path (to / of PierreFS) of the provided file.
 * \param[in]	inode	Inode that refers to the file
 * \param[in]	dentry	Dentry that refers to the file
 * \param[out]	path	The relative path that has been found
 * \param[in]	is_ours	Set to 1, it means that dentry & inode are local to PierreFS
 * \return 0 in case of a success, an error code otherwise
 * \note	It is possible not to provide a dentry (but not recommended). An inode must be provided then
 * \note	It is possible not to provide an inode. A dentry must be provided then
 * \warning	If no dentry is provided, the function might fail to find the path to the file even if it is on the PierreFS volume
 */
int get_relative_path(const struct inode *inode, const struct dentry *dentry, char *path, int is_ours);
/**
 * Get the relative path (to / of PierreFS) for the creation of the provided file.
 * \param[in]	dir		Inode that refers to the directory in which the file is to be created
 * \param[in]	dentry	Dentry that refers to the file to create in the directory
 * \param[out]	path	The relative path that has been found
 * \param[in]	is_ours	Set to 1, it means that dentry & inode are local to PierreFS
 * \return 0 in case of a success, an error code otherwise
 * \warning	This fuction relies on get_relative_path() and its limitations apply here
 */
int get_relative_path_for_file(const struct inode *dir, const struct dentry *dentry, char *path, int is_ours);
/**
 * Get the dentry representing the given path.
 * \param[in]	pathname	Pathname to lookup
 * \param[in]	flag		Flag to use for opening
 * \return dentry, or -err in case of error
 */
struct dentry* get_path_dentry(const char *pathname, int flag);
/**
 * Implementation taken from Linux kernel (and simplified). It's here to allow creation
 * of a link using pathname.
 * \param[in]	oldname	Target of the link
 * \param[in]	newname	Link path
 * \return	0 in case of a success, -err otherwise
 */
long link(const char *oldname, const char *newname);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a directory
 * using pathname.
 * \param[in]	pathname	Directory to create
 * \param[in]	mode		Mode to set to the directory (see mkdir man page)
 * \return	0 in case of a success, -err otherwise
 */
long mkdir(const char *pathname, int mode);
/**
 * Wrapper for mknod that allows creation of a FIFO file using pathname.
 * \param[in]	pathname	FIFO file to create
 * \param[in]	mode		Mode to set to the file (see mkfifo man page)
 * \return 	0 in case of a success, -err otherwise
 */
int mkfifo(const char *pathname, int mode);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a special
 * file using pathname.
 * \param[in]	pathname	File to create
 * \param[in]	mode		Mode to set to the file (see mknod man page)
 * \param[in]	dev			Special device
 * \return	0 in case of a success, -err otherwise
 */
long mknod(const char *pathname, int mode, unsigned dev);
/**
 * Implementation taken from Linux kernel. It's here to allow link readding (seems like
 * that's not the job vfs_readdlink is doing).
 * \param[in]	path	Link to read
 * \param[out]	buf		Link target
 * \param[in]	bufsiz	Output buffer size
 * \return	0 in case of a success, -err otherwise 
 */
long readlink(const char *path, char *buf, int bufsiz);
/**
 * Implementation taken from Linux kernel. It's here to allow creation of a symlink
 * using pathname.
 * \param[in]	oldname	Target of the symlink
 * \param[in]	newname	Symlink path
 * \return	0 in case of a success, -err otherwise
 */
long symlink(const char *oldname, const char *newname);
/**
 * Worker for debug purpose. It first checks opening mode and branch, and then call open.
 * This is used to catch bad calls to RO branch
 * \param[in]	pathname	File to open
 * \param[in]	flags		Flags for file opening (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_open(const char *pathname, int flags);
/**
 * Worker for debug purpose. It first checks opening mode and branch, and then call open.
 * This is used to catch bad calls to RO branch
 * \param[in]	pathname	File to open
 * \param[in]	flags		Flags for file opening (see open man page)
 * \param[in]	mode		Mode to set to the file (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_open_2(const char *pathname, int flags, mode_t mode);
/**
 * Worker for debug purpose. It checks if the file is to be created on the right branch
 * and then call creat
 * \param[in]	pathname	File to create
 * \param[in]	mode		Mode to set to the file (see open man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
struct file* dbg_creat(const char *pathname, mode_t mode);
/**
 * Worker for debug purpose. It checks if the directory is to be created on the right branch
 * and then call mkdir
 * \param[in]	pathname	Directory to create
 * \param[in]	mode		Mode to set to the directory (see mkdir man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mkdir(const char *pathname, mode_t mode);
/**
 * Worker for debug purpose. It checks if the special file is to be created on the right branch
 * and then call mknod
 * \param[in]	pathname	Special file to create
 * \param[in]	mode		Mode to set to the directory (see mknod man page)
 * \param[in]	dev		Attributes of the device (see mknod man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mknod(const char *pathname, mode_t mode, dev_t dev);
/**
 * Worker for debug purpose. It checks if the FIFO is to be created on the right branch
 * and then call mkfifo
 * \param[in]	pathname	FIFO to create
 * \param[in]	mode		Mode to set to the directory (see mkfifo man page)
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_mkfifo(const char *pathname, mode_t mode);
/**
 * Worker for debug purpose. It checks if the symlink is to be created on the right branch
 * and then call symlink
 * \param[in]	oldpath	Target of the symlink
 * \param[in]	newpath	Symlink to create
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_symlink(const char *oldpath, const char *newpath);
/**
 * Worker for debug purpose. It checks if the link is to be created on the right branch
 * and then call link
 * \param[in]	oldpath	Target of the link
 * \param[in]	newpath	Link to create
 * \return	-1 in case of a failure, 0 otherwise. errno is set
 */
int dbg_link(const char *oldpath, const char *newpath);

/* Functions in wh.c */
/**
 * Find the whiteout that might hide a file.
 * \param[in]	path	Relative path of the file to check
 * \param[out]	wh_path	Full path of the found whiteout
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int find_whiteout(const char *path, char *wh_path);
/**
 * Create a whiteout for each file contained in a directory.
 * \param[in]	path	Relative path of the directory where to hide files
 * \return	0 in case of a success, -err otherwise.
 * \note	In case directory doesn't exist on RO branch, it's a success
 */
int hide_directory_contents(const char *path);
/**
 * Check, using unionion, whether is directory is empty. If regarding union it's, ensure it really is.
 * \param[in]	path	Relative path of the directory to check
 * \param[in]	ro_path	Full path on RO branch of the directory
 * \param[in]	rw_path	Optional, full path on RW branch of the directory
 * \return	1 if empty, -err otherwise
 * \note	If you don't provide RW branch, no union will be done, it will just check for RO emptyness
 */
int is_empty_dir(const char *path, const char *ro_path, const char *rw_path);
/**
 * Unlink a file on RW branch, and whiteout possible file on RO branch.
 * \param[in]	path		Relative path of the file to unlink
 * \param[in]	rw_path		Full path of the file on RW branch to unlink
 * \param[in]	has_ro_sure	Optional, set to 1 if you check that file exists on RO
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int unlink_rw_file(const char *path, const char *rw_path, char has_ro_sure);
/**
 * Unlink the whiteout hidding a file.
 * \param[in]	path	Relative path of the file to "restore"
 * \return	0 in case of a success, -1 otherwise. errno is set
 */
int unlink_whiteout(const char *path);

#endif /* #ifdef __KERNEL__ */

#endif /* #ifndef __PIERREFS_H__ */
