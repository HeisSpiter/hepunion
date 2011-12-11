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

int get_file_attr_worker(const char *path, const char *real_path, struct kstat *stbuf) {
	int err;
	char me;
	struct kstat mest;
	char me_file[MAX_PATH];

	/* Look for a me file */
	me = (find_me(path, me_file, &mest) == 0);

	// Get attributes
	err = vfs_lstat(real_path, stbuf);
	if (!err) {
		// If me file was present, merge results
		if (me) {
			stbuf->uid = mest.uid;
			stbuf->gid = mest.gid;
			stbuf->atime = mest.atime;
			stbuf->mtime = mest.mtime;
			stbuf->ctime = mest.ctime;
			// Now we need to merge modes
			// First clean .me. modes
			mest.mode = clear_mode_flags(mest.mode);
			// Then, clean real file modes
			stbuf->mode &= ~VALID_MODES_MASK;
			// Finally, apply .me. modes
			stbuf->mode |= mest.mode;
		}
		return 0;
	}
	else {
		return err;
	}
}
