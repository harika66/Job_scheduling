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
 * @file pbs_demux.c
 * @brief
 * pbs_demux - handle I/O from multiple node job
 *
 *	Standard Out and Standard Error of each task is bound to
 *	stream sockets connected to pbs_demux which inputs from the
 *	various streams and writes to the JOB's out and error.
 */

#include <pbs_config.h> /* the master config generated by configure */
#include <pbs_version.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#if defined(FD_SET_IN_SYS_SELECT_H)
#include <sys/select.h>
#endif

#include "cmds.h"

enum rwhere { invalid,
	      new_out,
	      new_err,
	      old_out,
	      old_err };
struct routem {
	enum rwhere r_where;
	short r_nl;
	short r_first;
};
fd_set readset;
char *cookie = 0;

/**
 * @brief
 *	read data from socket
 *
 * @param[in] sock - socket
 * @param[in] prm  - routem structure pointer
 *
 * @return - Void
 *
 */
void
readit(int sock, struct routem *prm)
{
	int amt;
	char buf[256];
	FILE *fil;
	int i;
	char *pc;

	if (prm->r_where == old_out)
		fil = stdout;
	else
		fil = stderr;

	i = 0;
	if ((amt = read(sock, buf, 256)) > 0) {
		if (prm->r_first == 1) {

			/* first data on connection must be the cookie to validate it */

			i = strlen(cookie);
			if (strncmp(buf, cookie, i) != 0) {
				(void) close(sock);
				prm->r_where = invalid;
				FD_CLR(sock, &readset);
			}
			prm->r_first = 0;
		}
		for (pc = buf + i; pc < buf + amt; ++pc) {
#ifdef DEBUG
			if (prm->r_nl) {
				fprintf(fil, "socket %d: ", sock);
				prm->r_nl = 0;
			}
#endif /* DEBUG */
			putc(*pc, fil);
			if (*pc == '\n') {
				prm->r_nl = 1;
				fflush(fil);
			}
		}
	} else {
		close(sock);
		prm->r_where = invalid;
		FD_CLR(sock, &readset);
	}
	return;
}

int
main(int argc, char *argv[])
{
	struct timeval timeout;
	int i;
	int maxfd;
	int main_sock_out = 3;
	int main_sock_err = 4;
	int n;
	int newsock;
	pid_t parent;
	fd_set selset;
	struct routem *routem;

	/*test for real deal or just version and exit*/

	PRINT_VERSION_AND_EXIT(argc, argv);

	parent = getppid();
	cookie = getenv("PBS_JOBCOOKIE");
	if (cookie == 0) {
		fprintf(stderr, "%s: no PBS_JOBCOOKIE found in the env\n",
			argv[0]);
		exit(3);
	}
#ifdef DEBUG
	printf("Cookie found in environment: %s\n", cookie);
#endif

	maxfd = sysconf(_SC_OPEN_MAX);
	routem = (struct routem *) malloc(maxfd * sizeof(struct routem));
	if (routem == NULL) {
		fprintf(stderr, "%s: out of memory\n", argv[0]);
		exit(2);
	}
	for (i = 0; i < maxfd; ++i) {
		(routem + i)->r_where = invalid;
		(routem + i)->r_nl = 1;
		(routem + i)->r_first = 0;
	}
	(routem + main_sock_out)->r_where = new_out;
	(routem + main_sock_err)->r_where = new_err;

	FD_ZERO(&readset);
	FD_SET(main_sock_out, &readset);
	FD_SET(main_sock_err, &readset);

	if (listen(main_sock_out, 5) < 0) {
		perror("listen on out");
		exit(5);
	}

	if (listen(main_sock_err, 5) < 0) {
		perror("listen on err");
		exit(5);
	}

	while (1) {

		selset = readset;
		timeout.tv_usec = 0;
		timeout.tv_sec = 10;

		n = select(FD_SETSIZE, &selset, NULL, NULL, &timeout);
		if (n == -1) {
			if (errno == EINTR) {
				n = 0;
			} else {
				fprintf(stderr, "%s: select failed\n", argv[0]);
				exit(1);
			}
		} else if (n == 0) {
			if (kill(parent, 0) == -1) {
#ifdef DEBUG
				fprintf(stderr, "%s: Parent has gone, and so do I\n",
					argv[0]);
#endif /* DEBUG */
				break;
			}
		}

		for (i = 0; n && i < maxfd; ++i) {
			if (FD_ISSET(i, &selset)) { /* this socket has data */
				n--;
				switch ((routem + i)->r_where) {
					case new_out:
					case new_err:
						newsock = accept(i, 0, 0);
						(routem + newsock)->r_where = (routem + i)->r_where == new_out ? old_out : old_err;
						FD_SET(newsock, &readset);
						(routem + newsock)->r_first = 1;
						break;
					case old_out:
					case old_err:
						readit(i, routem + i);
						break;
					default:
						fprintf(stderr, "%s: internal error\n", argv[0]);
						exit(2);
				}
			}
		}
	}
	free(routem);
	return 0;
}
