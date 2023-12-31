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

#include "data_types.h"
#ifndef _RESOURCE_H
#define _RESOURCE_H

/*
 *	query_resources - query a pbs server for the resources it knows about
 *
 *	  pbs_sd - communication descriptor to pbs server
 *
 *	returns resdef list of resources
 */
std::unordered_map<std::string, resdef *> query_resources(int pbs_sd);

/*
 *	conv_rsc_type - convert server type number into resource_type struct
 *	  IN:  type - server type number
 *	  OUT: rtype - resource type structure
 *	returns nothing
 */
resource_type conv_rsc_type(int type);

/* find and return a resdef entry by name */
resdef *find_resdef(const std::string &name);

/*
 *  create resdef array based on a str array of resource names
 */
resdef **resdef_arr_from_str_arr(resdef **deflist, char **strarr);

/*
 * query the resource definition from the server and create derived
 * data structures.  Only query if required.
 */
bool update_resource_defs(int pbs_sd);

/* checks if a resource avail is set. */
int is_res_avail_set(schd_resource *res);

/* create a resource signature for a set of resources */
char *create_resource_signature(schd_resource *reslist, std::unordered_set<resdef *> &resources, unsigned int flags);

/* collect a unique list of resources from an array of requests */
std::unordered_set<resdef *> collect_resources_from_requests(resource_resv **resresv_arr);

/* convert an array of string resource names into resdefs */
std::unordered_set<resdef *> resstr_to_resdef(const std::unordered_set<std::string> &);
std::unordered_set<resdef *> resstr_to_resdef(const char *const *resstr);

/* filter function for filter_array().  Used to filter out host and vnode */
int no_hostvnode(void *v, void *arg);

/* add resdef to resdef array */
int add_resdef_to_array(resdef ***resdef_arr, resdef *def);

/* make a copy of a resdef array -- array itself is new memory,
 *        pointers point to the same thing
 */
resdef **copy_resdef_array(resdef **deflist);

/* update the def member in sort_info structures in conf */
void update_sorting_defs(void);

/* wrapper for pbs_statrsc */
struct batch_status *send_statrsc(int virtual_fd, char *id, struct attrl *attrib, char *extend);

#endif /* _RESOURCE_H */
