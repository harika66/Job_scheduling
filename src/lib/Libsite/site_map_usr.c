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
 * @file	site_map_usr.c
 * @brief
 * site_map_user - map user name on a named host to user name on this host
 * @note
 *	for those of us who operate in a world of consistant
 *	user names, this routine just returns the original name.
 *
 *	This routine is "left as an exercise for the reader."
 *	If you don't have consistant names, its up to you to replace
 *	this code with what will work for you.
 *
 *	The input parameters cannot be modified.   If the replacement routine
 *	is to change the user's name, the new value should be kept in a static
 *	character array of size PBS_MAXUSER+1.
 */

#include <pbs_config.h> /* the master config generated by configure */

#include <unistd.h>
#include "pbs_ifl.h"
#include "libpbs.h"
#include "portability.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_nodes.h"
#include "batch_request.h"
#include "svrfunc.h"
#include <string.h>

/* ARGSUSED */

/**
 * @brief
 *	map requestor user@host to "local" name
 *
 * @param[in] uname - username
 * @param[in] host - hostname
 *
 * @return	string
 * @retval	local name	success
 *
 */
char *
site_map_user(char *uname, char *host)
{
	return (uname);
}

char *
site_map_resvuser(char *uname, char *host)
{
	return (uname);
}
