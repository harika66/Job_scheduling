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

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_error.h"

/**
 * @file	attr_fn_b.c
 * @brief
 * 	This file contains functions for manipulating attributes of type
 *	boolean
 * @details
 * Each set has functions for:
 *	Decoding the value string to the machine representation.
 *	Encoding the machine representation of the value to a string
 *	Setting the value by =, + or - operators.
 *	Comparing a (decoded) value with the attribute value.
 *	Freeing the space malloc-ed to the attribute value.
 *
 * Some or all of the functions for an attribute type may be shared with
 * other attribute types.
 *
 * The prototypes are declared in "attribute.h"
 *
 * -------------------------------------------------------
 * Set of General functions for attributes of type boolean
 * -------------------------------------------------------
 */

static char *true_val = ATR_TRUE;
static char *false_val = ATR_FALSE;

/**
 * @brief
 * 	is_true_or_false - examine input for possible true/value values
 *
 * @param[in] val - value string
 *
 * @return	int
 * @retval	1 	for true
 * @retval	0 	for false
 * @retval	-1 	for error
 *
 */

int
is_true_or_false(char *val)
{
	if ((strcmp(val, true_val) == 0) ||
	    (strcmp(val, "TRUE") == 0) ||
	    (strcmp(val, "true") == 0) ||
	    (strcmp(val, "t") == 0) ||
	    (strcmp(val, "T") == 0) ||
	    (strcmp(val, "1") == 0) ||
	    (strcmp(val, "y") == 0) ||
	    (strcmp(val, "Y") == 0))
		return 1; /* true */
	else if ((strcmp(val, false_val) == 0) ||
		 (strcmp(val, "FALSE") == 0) ||
		 (strcmp(val, "false") == 0) ||
		 (strcmp(val, "f") == 0) ||
		 (strcmp(val, "F") == 0) ||
		 (strcmp(val, "0") == 0) ||
		 (strcmp(val, "n") == 0) ||
		 (strcmp(val, "N") == 0))
		return 0; /* false */
	else
		return (-1);
}
/**
 * @brief
 * 	decode_b - decode string into boolean attribute
 *
 *	String of "1" decodes to true, all else to false
 *
 * @param[in] patr - ptr to attribute to decode
 * @param[in] name - attribute name
 * @param[in] rescn - resource name or null
 * @param[out] val - string holding values for attribute structure
 *
 * @retval      int
 * @retval      0       if ok
 * @retval      >0      error number1 if error,
 * @retval      *patr   members set
 *
 */

int
decode_b(attribute *patr, char *name, char *rescn, char *val)
{
	int i;

	if ((val == NULL) || (strlen(val) == 0)) {
		ATR_UNSET(patr);
		patr->at_val.at_long = 0; /* default to false */
	} else {
		if ((i = is_true_or_false(val)) != -1)
			patr->at_val.at_long = i;
		else
			return (PBSE_BADATVAL);
		post_attr_set(patr);
	}
	return (0);
}

/**
 * @brief
 * 	encode_b - encode attribute of type ATR_TYPE_BOOL to attr_extern
 *
 * @param[in] patr - ptr to attribute to decode
 * @param[in] name - attribute name
 * @param[in] rescn - resource name or null
 * @param[out] val - string holding values for attribute structure
 *
 * @retval      int
 * @retval      0       if ok
 * @retval      >0      error number1 if error,
 * @retval      *patr   members set
 *
 */

/*ARGSUSED*/

int
encode_b(const attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)
{
	size_t ct;
	svrattrl *pal;
	char *value;

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0);

	if (attr->at_val.at_long) {
		value = true_val;
	} else {
		value = false_val;
	}
	ct = strlen(value) + 1;

	pal = attrlist_create(atname, rsname, ct);
	if (pal == NULL)
		return (-1);
	strcpy(pal->al_value, value);
	pal->al_flags = attr->at_flags;

	if (phead)
		append_link(phead, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;
	return (1);
}

/**
 * @brief
 * 	set_b - set attribute of type ATR_TYPE_BOOL
 *
 *	A=B --> A set to value of B
 *	A+B --> A = A | B  (inclusive or, turn on)
 *	A-B --> A = A & ~B  (and not, clear)
 *
 * @param[in]   attr - pointer to new attribute to be set (A)
 * @param[in]   new  - pointer to attribute (B)
 * @param[in]   op   - operator
 *
 * @return      int
 * @retval      0       if ok
 * @retval     >0       if error
 *
 */

int
set_b(attribute *attr, attribute *new, enum batch_op op)
{
	assert(attr && new && (new->at_flags &ATR_VFLAG_SET));

	switch (op) {
		case SET:
			attr->at_val.at_long = new->at_val.at_long;
			break;

		case INCR:
			attr->at_val.at_long =
				attr->at_val.at_long | new->at_val.at_long; /* "or" */
			break;

		case DECR:
			attr->at_val.at_long = attr->at_val.at_long &
					       ~new->at_val.at_long;
			break;

		default:
			return (PBSE_INTERNAL);
	}
	post_attr_set(attr);
	return (0);
}

/**
 * @brief
 *	comp_b - compare two attributes of type ATR_TYPE_BOOL
 *
 * @param[in] attr - pointer to attribute structure
 * @param[in] with - pointer to attribute structure
 *
 * @return      int
 * @retval      0       if the set of strings in "with" is a subset of "attr"
 * @retval      1       otherwise
 *
 */

int
comp_b(attribute *attr, attribute *with)
{
	if (!attr || !with)
		return (1);
	if (((attr->at_val.at_long == 0) && (with->at_val.at_long == 0)) ||
	    ((attr->at_val.at_long != 0) && (with->at_val.at_long != 0)))
		return (0);
	else
		return (1);
}

/*
 * free_b - use free_null() to (not) free space
 */

/**
 * @brief	Attribute setter function for boolean type values
 *
 * @param[in]	pattr	-	pointer to attribute being set
 * @param[in]	value	-	value to be set
 * @param[in]	op		-	operation to do
 *
 * @return	void
 *
 * @par MT-Safe: No
 * @par Side Effects: None
 *
 */
void
set_attr_b(attribute *pattr, long val, enum batch_op op)
{
	switch (op) {
		case SET:
			pattr->at_val.at_long = val;
			break;

		case INCR:
			pattr->at_val.at_long = pattr->at_val.at_long | val; /* "or" */
			break;

		case DECR:
			pattr->at_val.at_long = pattr->at_val.at_long & ~val;
			break;

		default:
			return;
	}
	post_attr_set(pattr);
}
