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
 * @file	set_resources
 * @brief
 *	Append entries to the attribute list that are from the resource list.
 * 	If the add flag is set, append the resource regardless. Otherwise, append
 * 	it only if it is not already on the list.
 *
 */

#include <pbs_config.h> /* the master config generated by configure */
#include "cmds.h"
#include "attribute.h"

static int allowresc = 1;

/**
 * @brief
 *	 Append entries to the attribute list that are from the resource list.
 * 	If the add flag is set, append the resource regardless. Otherwise, append
 * 	it only if it is not already on the list.
 *
 * @param[in] attrib - pointer to attribute list
 * @param[in] resources - resources
 * @param[in] add - flag to indicate appending resource
 * @param[in] erptr - resource
 *
 * @return	int
 * @retval	0	resource set
 * @retval	1	resource not set / error
 *
 */

int
set_resources(struct attrl **attrib, const char *resources, int add, char **erptr)
{
	char *eq, *v, *e;
	char *r = (char *) resources;
	char *str;
	struct attrl *attr, *ap, *priorap;
	int i, found, len;
	int haveresc = 0;

	while (*r != '\0') {

		/* Skip any leading whitespace */
		while (isspace((int) *r))
			r++;

		/* Get the resource name */
		eq = r;
		while (*eq != '=' && *eq != ',' && *eq != '\0')
			eq++;

		/* Make sure there is a resource name */
		if (r == eq) {
			*erptr = r;
			return (1);
		}

		/*
		 * Count the number of non-space character that make up the
		 * resource name.  Count only up to the last character before the
		 * separator ('\0', ',' or '=').
		 */
		for (str = r, len = 0; str < eq && !isspace((int) *str); str++)
			len++;

		/* If separated by an equal sign, get the value */
		if (*eq == '=') {
			char *t;

			t = eq + 1;
			while (isspace((int) *t))
				++t;

			/* Added a special case for preempt_targets as this resource is of
			 * type array string and can have comma seperated resources and queues as its value.
			 */
			if ((r != NULL) && (strncmp(r, "preempt_targets", 15) == 0) &&
			    (t != NULL)) {
				e = t;
				while (*e != '\0') {
					e++;
				}
				v = malloc(e - t + 1);
				if (v == NULL)
					return (-1);
				strncpy(v, t, e - t);
				v[e - t] = '\0';
			} else {
				/* Normal resource: if no error, v will be on the heap */
				if ((i = pbs_quote_parse(t, &v, &e, QMGR_NO_WHITE_IN_VALUE)) != 0) {
					*erptr = e;
					return (i);
				}
			}
		} else {
			v = NULL;
		}

		/* Allocate memory for the attrl structure */
		attr = new_attrl();
		if (attr == NULL) {
			free(v);
			fprintf(stderr, "Out of memory\n");
			return 2;
		}

		/* Allocate memory for the attribute name and copy */
		str = (char *) malloc(strlen(ATTR_l) + 1);
		if (str == NULL) {
			free(v);
			free_attrl(attr);
			fprintf(stderr, "Out of memory\n");
			return 2;
		}
		strcpy(str, ATTR_l);
		attr->name = str;

		/* Allocate memory for the resource name and copy */
		str = (char *) malloc(len + 1);
		if (str == NULL) {
			free(v);
			free_attrl(attr);
			fprintf(stderr, "Out of memory\n");
			return 2;
		}
		strncpy(str, r, len);
		str[len] = '\0';
		attr->resource = str;

		/* insert value */
		if (v != NULL) {
			attr->value = v;
		} else {
			str = (char *) malloc(1);
			if (str == NULL) {
				free_attrl(attr);
				fprintf(stderr, "Out of memory\n");
				return 2;
			}
			str[0] = '\0';
			attr->value = str;
		}

		/* if we find "resc" in the command lines, disallow it in directives */
		if (strcasecmp(attr->resource, "resc") == 0) {
			haveresc = 1;
			if (add)
				allowresc = 0;
		}

		/* Put it on the attribute list */
		/* If the argument add is true, add to the list regardless.
		 * Otherwise, add it to the list only if the resource name
		 * is not already on the list.
		 */
		found = FALSE;
		attr->next = NULL;
		if (*attrib == NULL) {
			*attrib = attr;
		} else {
			ap = *attrib;
			while (ap != NULL) {
				priorap = ap;
				if (strcmp(ap->name, ATTR_l) == 0 &&
				    strcmp(ap->resource, attr->resource) == 0)
					found = TRUE;
				ap = ap->next;
			}
			/* have to special case "resc" since it can appear multiple times */
			if (add || !found || (haveresc && allowresc))
				priorap->next = attr;
		}

		/* Get ready for next resource/value pair */
		if (v != NULL)
			r = e;
		else
			r = eq;
		if (*r == ',') {
			r++;
			if (*r == '\0') {
				*erptr = r;
				return (1);
			}
		}
	}
	return (0);
}
