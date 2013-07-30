/**
 * \file hepunion_tests_tests.c
 * \brief Regtests for the HEPunion file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 19-July-2013
 * \copyright GNU General Public License - GPL
 */

#include "hepunion_tests.h"

static unsigned int failed_tests = 0;
static unsigned int run_tests = 0;

/* Helper function that will test that, after a test, all
 * the read-only files are still fine
 */
static void check_root_tree() {
	struct stat buf;

	/* Get data for the dir */
	test_ok(stat("root/", &buf) != -1, "");

	/* Check data */
	test_ok(S_ISDIR(buf.st_mode), "st_mode = %#X\n", buf.st_mode);
	test_ok(buf.st_nlink == 3, "st_nlinks = %lu\n", buf.st_nlink);
	test_ok(buf.st_gid == 0, "gid = %d\n", buf.st_gid);
	test_ok(buf.st_uid == 0, "uid = %d\n", buf.st_uid);
	test_ok(buf.st_blocks == 8, "blocks = %lu\n", buf.st_blocks);
	test_ok(buf.st_size == 4096, "size = %lu\n", buf.st_size);

	/* Get data for the file */
	test_ok(stat("root/ro_file", &buf) != -1, "");

	/* Check data */
	test_ok(S_ISREG(buf.st_mode), "st_mode = %#X\n", buf.st_mode);
	test_ok(buf.st_nlink == 1, "st_nlinks = %lu\n", buf.st_nlink);
	test_ok(buf.st_gid == 0, "gid = %d\n", buf.st_gid);
	test_ok(buf.st_uid == 0, "uid = %d\n", buf.st_uid);
	test_ok(buf.st_blocks == 0, "blocks = %lu\n", buf.st_blocks);
	test_ok(buf.st_size == 0, "size = %lu\n", buf.st_size);

	/* Get data for the directory */
	test_ok(stat("root/ro_dir", &buf) != -1, "");

	/* Check data */
	test_ok(S_ISDIR(buf.st_mode), "st_mode = %#X\n", buf.st_mode);
	test_ok(buf.st_nlink == 2, "st_nlinks = %lu\n", buf.st_nlink);
	test_ok(buf.st_gid == 0, "gid = %d\n", buf.st_gid);
	test_ok(buf.st_uid == 0, "uid = %d\n", buf.st_uid);
	test_ok(buf.st_blocks == 8, "blocks = %lu\n", buf.st_blocks);
	test_ok(buf.st_size == 4096, "size = %lu\n", buf.st_size);

	/* Get data for the file */
	test_ok(stat("root/ro_dir/ro_file", &buf) != -1, "");

	/* Check data */
	test_ok(S_ISREG(buf.st_mode), "st_mode = %#X\n", buf.st_mode);
	test_ok(buf.st_nlink == 1, "st_nlinks = %lu\n", buf.st_nlink);
	test_ok(buf.st_gid == 0, "gid = %d\n", buf.st_gid);
	test_ok(buf.st_uid == 0, "uid = %d\n", buf.st_uid);
	test_ok(buf.st_blocks == 0, "blocks = %lu\n", buf.st_blocks);
	test_ok(buf.st_size == 0, "size = %lu\n", buf.st_size);
}

void do_tests(const char * working_dir) {
	unreferenced_parameter(working_dir);

	/* Perform a sanity check before we start */
	check_root_tree();

	/* TODO */

	/* End of tests */
	debug_out("%u tests executed, %u failed\n", run_tests, failed_tests);
}
