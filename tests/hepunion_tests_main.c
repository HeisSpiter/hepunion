/**
 * \file hepunion_tests_main.c
 * \brief Regtests for the HEPunion file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 19-July-2013
 * \copyright GNU General Public License - GPL
 */

#include "hepunion_tests.h"

static inline int check_tree_dir(const char * path) {
	char current_dir[PATH_MAX];
	struct stat buf;

	strncpy(current_dir, path, sizeof(current_dir) / sizeof(current_dir[0]));
	return stat(current_dir, &buf);
}

int main(int argc, char **argv) {
	char working_dir[PATH_MAX];

	/* Check we are running as root */
	debug_return(geteuid() == 0, EACCES, "%s", "Please, run the program as root!\n");

	/* Get directory in which tests are to be run */
	if (argc > 1) {
		strncpy(working_dir, argv[1], sizeof(working_dir) / sizeof(working_dir[0]));
		/* And try to move to it */
		debug_return(chdir(working_dir) != -1, errno, "working_dir=\"%s\" errno=%d\n", working_dir, errno);
	} else {
		getcwd(working_dir, sizeof(working_dir));
		/* Nothing more to do, we're still there */
	}

	debug_out("Will execute tests on: %s\n", working_dir);

	/* Quickly check whether tree looks good */
	debug_return(check_tree_dir("export/") != -1, errno, "Failed stat() on export directory, errno = %d\n", errno);
	debug_return(check_tree_dir("root/") != -1, errno, "Failed stat() on root directory, errno = %d\n", errno);
	debug_return(check_tree_dir("snapshot/") != -1, errno, "Failed stat() on snapshot directory, errno = %d\n", errno);

	/* Start tests */
	do_tests(working_dir);

	return 0;
}
