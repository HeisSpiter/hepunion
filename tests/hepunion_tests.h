/**
 * \file hepunion_tests.h
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

void do_tests(const char * working_dir);

#define debug_return(e, r, f, ...) 															\
	if (!(e)) {																				\
		fprintf(stderr, "%s:%d \"%s\" failed: " f, __FILE__, __LINE__, #e, ## __VA_ARGS__);	\
		return -r;																			\
	}

#define debug_out(f, ...)											\
	fprintf(stdout, "%s:%d " f, __FILE__, __LINE__, ## __VA_ARGS__)

#define test_ok(e, f, ...) 																	\
	++run_tests;																			\
	if (!(e)) {																				\
		++failed_tests;																		\
		fprintf(stderr, "%s:%d \"%s\" failed: " f, __FILE__, __LINE__, #e, ## __VA_ARGS__);	\
	}

#define unreferenced_parameter(p)	\
	(void)p
