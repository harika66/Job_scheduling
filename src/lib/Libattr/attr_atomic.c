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

#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"

/**
 * @file	attr_atomic.c
 * @brief
 * 	This file contains general functions for manipulating an attribute array.
 *
 * @par	Included is:
 *	attr_atomic_set()
 *	attr_atomic_kill()
 *
 * 	The prototypes are declared in "attr_func.h"
 */

/* Global Variables */

extern int resc_access_perm; /* see lib/Libattr/attr_fn_resc.c */

/**
 * @brief
 *	atomically set a attribute array with values from a svrattrl
 *
 * @param[in] plist - Pointer to list of attributes to set
 * @param[in] old - Pointer to the original/old attribute
 * @param[in] new - Pointer to the new/updated attribute
 * @param[in] pdef_idx - Search index for the attribute def array
 * @param[in] pdef - Pointer to the attribute definition
 * @param[in] limit - The index up to which to search for a definition
 * @param[in] unkn - Whether to allow unknown resources or not
 * @param[in] privil - Permissions of the caller requesting the operation
 * @param[out] badattr - Pointer to the attribute index in case of a failed
 * 			operation
 *
 * @return 	int
 * @retval 	PBSE_NONE 	on success
 * @retval 	PBS error code 	otherwise
 *
 */

int
attr_atomic_set(struct svrattrl *plist, attribute *old, attribute *new, void *pdef_idx, attribute_def *pdef, int limit, int unkn, int privil, int *badattr)
{
	int acc;
	int index;
	int listidx;
	resource *prc;
	int rc;
	attribute temp;
	int privil2;

	for (index = 0; index < limit; index++)
		clear_attr(new + index, pdef + index);

	resc_access_perm = privil; /* set privilege for decode_resc()  */

	for (listidx = 1, rc = PBSE_NONE; (rc == PBSE_NONE) && (plist != NULL); plist = (struct svrattrl *) GET_NEXT(plist->al_link), listidx++) {

		if ((index = find_attr(pdef_idx, pdef, plist->al_name)) < 0) {
			if (unkn < 0) { /*unknown attr isn't allowed*/
				rc = PBSE_NOATTR;
				break;
			} else
				index = unkn; /* if unknown attr are allowed */
		}

		/* have we privilege to set the attribute ? */
		privil2 = privil;
		if ((plist->al_flags & ATR_VFLAG_HOOK)) {
			privil2 = ATR_DFLAG_USRD | ATR_DFLAG_USWR |
				  ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
				  ATR_DFLAG_MGRD | ATR_DFLAG_MGWR |
				  ATR_DFLAG_SvWR;
		}
		resc_access_perm = privil2; /* set privilege for decode_resc() */

		acc = (pdef + index)->at_flags & ATR_DFLAG_ACCESS;
		if ((acc & privil2 & ATR_DFLAG_WRACC) == 0) {
			if (privil2 & ATR_DFLAG_SvWR) {
				/*from a daemon, just ignore this attribute*/
				continue;
			} else {
				/*from user, error if can't write attribute*/
				rc = PBSE_ATTRRO;
				break;
			}
		}

		/* decode new value */

		clear_attr(&temp, pdef + index);
		if ((rc = (pdef + index)->at_decode(&temp, plist->al_name, plist->al_resc, plist->al_value)) != 0) {
			/* Even if the decode failed, it is possible for list types to
			 * have allocated some memory.  Call at_free() to free that.
			 */
			(pdef + index)->at_free(&temp);
			if ((rc == PBSE_UNKRESC) && (unkn > 0)) {
				rc = PBSE_NONE; /* ignore the "error" */
				continue;
			} else
				break;
		}

		/* duplicate current value, if set AND not already dup-ed */

		if (((old + index)->at_flags & ATR_VFLAG_SET) &&
		    !((new + index)->at_flags & ATR_VFLAG_SET)) {
			if ((rc = (pdef + index)->at_set(new + index, old + index, SET)) != 0)
				break;
			/*
			 * we need to know if the value is changed during
			 * the next step, so clear MODIFY here; including
			 * within resources.
			 */
			(new + index)->at_flags &= ~ATR_MOD_MCACHE;
			if ((new + index)->at_type == ATR_TYPE_RESC) {
				prc = (resource *) GET_NEXT((new + index)->at_val.at_list);
				while (prc) {
					prc->rs_value.at_flags &= ~ATR_MOD_MCACHE;
					prc = (resource *) GET_NEXT(prc->rs_link);
				}
			}
		}

		/* update new copy with temp, MODIFY is set on ones changed */

		if ((plist->al_op != INCR) && (plist->al_op != DECR) &&
		    (plist->al_op != SET))
			plist->al_op = SET;

		if (temp.at_flags & ATR_VFLAG_SET) {
			rc = (pdef + index)->at_set(new + index, &temp, plist->al_op);
			if (rc) {
				(pdef + index)->at_free(&temp);
				break;
			}
		} else if (temp.at_flags & ATR_VFLAG_MODIFY) {
			(pdef + index)->at_free(new + index);
			(new + index)->at_flags |= ATR_MOD_MCACHE; /* SET was removed by at_free */
		}

		(pdef + index)->at_free(&temp);
	}

	if (rc != PBSE_NONE) {
		*badattr = listidx;
		for (index = 0; index < limit; index++)
			(pdef + index)->at_free(new + index);
	}

	return rc;
}

/**
 * @brief
 * 	attr_atomic_kill - kill (free) a temporary attribute array which
 *	was set up by attr_atomic_set().
 *
 *	at_free() is called on each element on the array, then
 *	the array itself is freed.
 *
 * @param[in] temp - pointer  to attribute structure
 * @param[in] pdef - pointer to attribute_def structure
 * @param[in] limit -  Last attribute in the list
 *
 * @return 	Void
 *
 */

void
attr_atomic_kill(attribute *temp, attribute_def *pdef, int limit)
{
	int i;

	for (i = 0; i < limit; i++)
		(pdef + i)->at_free(temp + i);
	free(temp);
}

/**
 * @brief
 * 	attr_atomic_copy - make a copy of the attributes in attribute array 'from' to the attribute array 'to'.
 * 				'to' must be preallocated.  Attributes that use set_null() as their set function
 * 				are not meant to be set via the attribute framework.  Leave these attributes
 * 				alone in 'to'
 *
 * 	@param[out] to - attribute array copied into
 * 	@param[in] from - attribute array copied from
 * 	@param[in] pdef - pointer to attribute_def structure
 * 	@param[in] limit - Last attribute in the list
 */

void
attr_atomic_copy(attribute *to, attribute *from, attribute_def *pdef, int limit)
{
	int i;
	for (i = 0; i < limit; i++) {
		if (((to + i)->at_flags & ATR_VFLAG_SET) && (pdef + i)->at_set != set_null)
			(pdef + i)->at_free(to + i);

		if ((pdef + i)->at_set != set_null)
			clear_attr(to + i, pdef + i);
		if ((from + i)->at_flags & ATR_VFLAG_SET) {
			(pdef + i)->at_set((to + i), (from + i), SET);
			(to + i)->at_flags = (from + i)->at_flags;
		}
	}
}
