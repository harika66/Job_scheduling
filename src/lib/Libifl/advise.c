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

#include <pbs_config.h> /* the master config generated by configure */

#include <stdarg.h>
#include <stdio.h>
/**
 * @file	advise.c
 */

/**
 * @brief - prints advise from user (variable  argument list ) to standard error file.
 *
 * @param[in] who - user name
 * @param[in] variable arguments
 *
 * @return	Void
 *
 */
void
advise(char *who, ...)
{
	va_list args;
	char *fmt;

#ifndef NDEBUG

	va_start(args, who);

	if (who == NULL || *who == '\0')
		fprintf(stderr, "advise:\n");
	else
		fprintf(stderr, "advise from %s:\n", who);
	fmt = va_arg(args, char *);
	fputs("\t", stderr);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	fputc('\n', stderr);
	fflush(stderr);
#endif
}
