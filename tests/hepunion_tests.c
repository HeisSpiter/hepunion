/**
 * \file hepunion_tests.c
 * \brief Regtests for the HEPunion file system
 * \author Pierre Schweitzer <pierre.jean.schweitzer@cern.ch>
 * \version 1.0
 * \date 19-July-2013
 * \copyright GNU General Public License - GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/limits.h>

static inline int check_tree_dir(const char * path) {
	char current_dir[PATH_MAX];
	struct stat buf;

	strncpy(current_dir, path, sizeof(current_dir) / sizeof(current_dir[0]));
	return stat(current_dir, &buf);
}

int main(int argc, char **argv) {
	char working_dir[PATH_MAX];

	/* Check we are running as root */
	if (geteuid() != 0) {
		fprintf(stderr, "Please, run the program as root!\n");
		return -EACCES;
	}

	/* Get directory in which tests are to be run */
	if (argc > 1) {
		strncpy(working_dir, argv[1], sizeof(working_dir) / sizeof(working_dir[0]));
		/* And try to move to it */
		if (chdir(working_dir) == -1) {
			fprintf(stderr, "Failed chdir() on test directory\n");
			return -errno;
		}
	} else {
		getcwd(working_dir, sizeof(working_dir));
		/* Nothing more to do, we're still there */
	}

	fprintf(stdout, "Will execute tests on: %s\n", working_dir);

	/* Quickly check whether tree looks good */
	if (check_tree_dir("export/") == -1) {
		fprintf(stderr, "Failed stat() on export directory\n");
		return -errno;
	}
	if (check_tree_dir("root/") == -1) {
		fprintf(stderr, "Failed stat() on root directory\n");
		return -errno;
	}
	if (check_tree_dir("snapshot/") == -1) {
		fprintf(stderr, "Failed stat() on snapshot directory\n");
		return -errno;
	}

	/* Start tests */

	return 0;
}
