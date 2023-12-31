/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file    chk_tree.c
 *
 * @brief
 * 		chk_tree.c - Check permissions on the PBS Tree Built
 *		Files should be owned by root, and not world writable.
 *
 * Functions included are:
 * 	main()
 * 	log_err()
 */

#include <pbs_config.h> /* the master config generated by configure */
#include "pbs_version.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "cmds.h"
#include "portability.h"
#include "log.h"

/**
 * @brief
 * 		main	-	The main function of chk_tree
 *
 * @param[in]	argc	-	argument count
 * @param[in]	argv	-	argument variables.
 *
 * @return	int
 * @retval	0	: success
 * @retval	!=0	: some error.
 */
int
main(int argc, char *argv[])
{
	int err = 0;
	int i;
	int j;
	int chk_file_sec();
	int dir = 0;
	int no_err = 0;
	int sticky = 0;
	extern int optind;

	/*the real deal or output pbs_version and exit?*/
	PRINT_VERSION_AND_EXIT(argc, argv);
	if (set_msgdaemonname("chk_tree")) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	set_logfile(stderr);

	while ((i = getopt(argc, argv, "dns-:")) != EOF) {
		switch (i) {
			case 'd':
				dir = 1;
				break;
			case 'n':
				no_err = 1;
				break;
			case 's':
				sticky = 1;
				break;
			default:
				err = 1;
		}
	}

	if (err || (optind == argc)) {
		fprintf(stderr, "Usage %s -d -s -n path ...\n\twhere:\t-d indicates directory (file otherwise)\n\t\t-s indicates world write allowed if sticky set\n\t\t-n indicates do not return the error status, exit with 0\n", argv[0]);
		fprintf(stderr, "      %s --version display version, exit with 0\n", argv[0]);
		return 1;
	}

	for (i = optind; i < argc; ++i) {
#ifdef WIN32
		/* we're not checking fullpath */
		j = chk_file_sec(argv[i], dir, sticky, WRITES_MASK ^ FILE_WRITE_EA, 0);
#else
		j = chk_file_sec(argv[i], dir, sticky, S_IWGRP | S_IWOTH, 1);
#endif
		if (j)
			err = 1;
	}

	if (no_err)
		return 0;
	else
		return (err);
}
