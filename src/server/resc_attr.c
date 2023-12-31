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
 *
 * @brief
 * 	Functions relation to the Track Job Request and job tracking.
 *
 */

#include <pbs_config.h> /* the master config generated by configure */
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "pbs_ifl.h"
#include "server_limits.h"
#include <string.h>
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "grunt.h"
#include "pbs_share.h"
#include "server.h"
#ifndef PBS_MOM
#include "queue.h"
#endif /* PBS_MOM */

extern char *find_aoe_from_request(resc_resv *);
/**
 * @brief
 * 		ctnodes	-	count the num of nodes from attribute struct holding value
 *
 * @param[in]	spec	-	attribute struct holding value
 *
 * @return	num of nodes
 */
int
ctnodes(char *spec)
{
	int ct = 0;
	char *pc;

	while (1) {

		while (isspace((int) *spec))
			++spec;

		if (isdigit((int) *spec)) {
			pc = spec;
			while (isdigit((int) *pc))
				++pc;
			if (!isalpha((int) *pc))
				ct += atoi(spec);
			else
				++ct;
		} else
			++ct;
		if ((pc = strchr(spec, '+')) == NULL)
			break;
		spec = pc + 1;
	}
	return (ct);
}

/**
 * @brief
 * 		set_node_ct = set node count
 * @par
 *		This is the "at_action" routine for the resource "nodes".
 *		When the resource_list attribute changes, then set/update
 *		the value of the resource "nodect" for use by the scheduler.
 *		Also updates "ncpus" in most circumstances.
 *
 * @param[in]	pnodesp	-	pointer to resource
 * @param[in,out]	pattr	-	pointer to attribute
 * @param[in]	pobj	-	unused here.
 * @param[in]	type	-	unused here.
 * @param[in]	actmode	-	mode of action routine
 */

int
set_node_ct(resource *pnodesp, attribute *pattr, void *pobj, int type, int actmode)
{
#ifndef PBS_MOM
	int nn;	      /* num of nodes */
	int nt;	      /* num of tasks (processes) */
	int hcpp = 0; /* has :ccp in string */
	long nc;
	resource *pnct;
	resource *pncpus;
	resource_def *pndef;

	if ((actmode == ATR_ACTION_RECOV) ||
	    ((is_attr_set(&pnodesp->rs_value)) == 0))
		return (0);

	/* first validate the spec */

	if ((nn = validate_nodespec(pnodesp->rs_value.at_val.at_str)) != 0)
		return nn;

	/* Set "nodect" to count of nodes in "nodes" */

	pndef = &svr_resc_def[RESC_NODECT];
	if (pndef == NULL)
		return (PBSE_SYSTEM);

	if ((pnct = find_resc_entry(pattr, pndef)) == NULL) {
		if ((pnct = add_resource_entry(pattr, pndef)) == 0)
			return (PBSE_SYSTEM);
	}

	nn = ctnodes(pnodesp->rs_value.at_val.at_str);
	pnct->rs_value.at_val.at_long = nn;
	post_attr_set(&pnct->rs_value);

	/* find the number of cpus specified in the node string */

	nt = ctcpus(pnodesp->rs_value.at_val.at_str, &hcpp);

	/* Is "ncpus" set as a separate resource? */

	pndef = &svr_resc_def[RESC_NCPUS];
	if (pndef == NULL)
		return (PBSE_SYSTEM);
	if ((pncpus = find_resc_entry(pattr, pndef)) == NULL) {
		if ((pncpus = add_resource_entry(pattr, pndef)) == 0)
			return (PBSE_SYSTEM);
	}

	if (((pncpus->rs_value.at_flags & (ATR_VFLAG_SET | ATR_VFLAG_DEFLT)) == ATR_VFLAG_SET) && (actmode == ATR_ACTION_NEW)) {
		/* ncpus is already set and not a default and new job */

		nc = pncpus->rs_value.at_val.at_long;
		if (hcpp && (nt != pncpus->rs_value.at_val.at_long)) {
			/* if cpp string specificed, this is an error */
			return (PBSE_BADATVAL);
		} else if ((nc % nt) != 0) {
			/* ncpus must be multiple of number of tasks */
			return (PBSE_BADATVAL);
		}

	} else {
		/* ncpus is not set or not a new job (qalter being done) */
		/* force ncpus to the correct thing */
		pncpus->rs_value.at_val.at_long = nt;
		post_attr_set(&pncpus->rs_value);
	}

#endif /* not MOM */
	return (0);
}

struct place_words {
	char *pw_word;	   /* place keyword */
	short pw_oneof;	   /* bit mask for which cannot be together */
	short pw_equalstr; /* one if word has following "=value" */
} place_words[] = {
	{PLACE_Group, 0, 1},
	{PLACE_Excl, 2, 0},
	{PLACE_ExclHost, 2, 0},
	{PLACE_Shared, 2, 0},
	{PLACE_Free, 1, 0},
	{PLACE_Pack, 1, 0},
	{PLACE_Scatter, 1, 0},
	{PLACE_VScatter, 1, 0}};
/**
 * @brief
 * 		decode_place	-	not used.
 */

int
decode_place(attribute *patr, char *name, char *rescn, char *val)
{
#ifndef PBS_MOM
	int have_oneof = 0;
	int i;
	size_t ln;
	char h;
	char *pc;
	char *px;
	struct resource_def *pres;

	pc = val;

	while (1) {
		while (isspace((int) *pc))
			++pc;
		if (*pc == '\0' || !isalpha((int) *pc))
			return PBSE_BADATVAL;
		/* found start of word,  look for end of word */
		px = pc + 1;
		while (isalpha((int) *px))
			px++;

		for (i = 0; i < sizeof(place_words) / sizeof(place_words[0]); ++i) {
			if (strlen(place_words[i].pw_word) >= (size_t) (px - pc))
				ln = strlen(place_words[i].pw_word);
			else
				ln = (size_t) (px - pc);
			if (strncasecmp(pc, place_words[i].pw_word, ln) == 0) {
				break;
			}
		}
		if (i == sizeof(place_words) / sizeof(place_words[0]))
			return PBSE_BADATVAL;

		if (place_words[i].pw_oneof & have_oneof)
			return PBSE_BADATVAL;
		have_oneof |= place_words[i].pw_oneof;

		if (place_words[i].pw_equalstr) {
			if (*px != '=')
				return PBSE_BADATVAL;
			pc = ++px;
			while ((isalnum((int) *px) || (*px == '_') || (*px == '-')) &&
			       (*px != ':'))
				++px;
			if (pc == px)
				return PBSE_BADATVAL;
			/* now need to see if the value is a valid resource/type */
			h = *px;
			*px = '\0';
			pres = find_resc_def(svr_resc_def, pc);
			if (pres == NULL)
				return PBSE_UNKRESC;
			if ((pres->rs_type != ATR_TYPE_STR) &&
			    (pres->rs_type != ATR_TYPE_ARST))
				return PBSE_RESCNOTSTR;
			*px = h;

			if (*px == '\0')
				break;
			else if (*px != ':')
				return PBSE_BADATVAL;
		}
		pc = px;
		if (*pc == '\0')
			break;
		else if (*pc != ':')
			return PBSE_BADATVAL;
		pc++;
	}

#endif /* not PBS_MOM */

	return (decode_str(patr, name, rescn, val));
}

/**
 * @brief
 * 		to_kbsize - decode a "size" string to a value in kilobytes
 *
 * @param[in]	val	-	"size" string
 *
 * @return	long
 * @retval	value in kilobytes
 */

long long
to_kbsize(char *val)
{
	int havebw = 0;
	long long sv_num;
	int sv_shift = 0;
	char *pc;

	sv_num = strtol(val, &pc, 0);
	if (pc == val) /* no numeric part */
		return (0);

	switch (*pc) {
		case '\0':
			break;
		case 'k':
		case 'K':
			sv_shift = 10;
			break;
		case 'm':
		case 'M':
			sv_shift = 20;
			break;
		case 'g':
		case 'G':
			sv_shift = 30;
			break;
		case 't':
		case 'T':
			sv_shift = 40;
			break;
		case 'p':
		case 'P':
			sv_shift = 50;
			break;
		case 'b':
		case 'B':
			havebw = 1;
			break;
		case 'w':
		case 'W':
			havebw = 1;
			sv_num *= SIZEOF_WORD;
			break;

		default:
			return (0); /* invalid string */
	}
	if (*pc != '\0')
		pc++;
	if (*pc != '\0') {
		if (havebw)
			return (0); /* invalid string */
		switch (*pc) {
			case 'b':
			case 'B':
				break;
			case 'w':
			case 'W':
				sv_num *= sizeof(int);
				break;
			default:
				return (0);
		}
	}

	if (sv_shift == 0) {
		sv_num = (sv_num + 1023) >> 10;
	} else {
		sv_num = sv_num << (sv_shift - 10);
	}
	return (sv_num);
}

/**
 * @brief
 * 		preempt_targets_action - A function which is used to validate the <attribute>
 *                          out of "<attribute>=<value>" pair assigned to preempt_targets
 *
 * @param[in]	presc    -       pointer to resource
 * @param[in]   pattr    -       pointer to attribute
 * @param[in]   pobj     -       pointer to job or reservation
 * @param[in]   type     -       if job or reservation
 * @param[in]   actmode  -       mode of action routine
 *
 * @return	int
 * @retval	PBSE_NONE	: success
 * @retval	PBSE_BADATVAL	: if a non existent attribute is given
 */

int
preempt_targets_action(resource *presc, attribute *pattr, void *pobject, int type, int actmode)
{
	char *name;
	char *p;
	int i;
	char *res_name = NULL;
	resource_def *resdef = NULL;
	char ch;

	if ((actmode == ATR_ACTION_FREE) || (actmode == ATR_ACTION_RECOV))
		return PBSE_NONE;

	if (!is_attr_set(pattr))
		return PBSE_NONE;

	if (presc->rs_value.at_val.at_arst == NULL)
		return PBSE_BADATVAL;

	for (i = 0; i < presc->rs_value.at_val.at_arst->as_usedptr; ++i) {
		name = presc->rs_value.at_val.at_arst->as_string[i];

		if (!strncasecmp(name, TARGET_NONE, strlen(TARGET_NONE))) {
			if (presc->rs_value.at_val.at_arst->as_usedptr > 1)
				return PBSE_BADATVAL;
			return PBSE_NONE;
		}
		p = strpbrk(name, ".=");
		if (p) {
			ch = *p;
			*p = '\0';
			if (!(strcasecmp(name, ATTR_l))) {
				*p = ch;
				res_name = p + 1;
				p = strpbrk(res_name, "=");
				if (p) {
					ch = *p;
					*p = '\0';
					resdef = find_resc_def(svr_resc_def, res_name);
					*p = ch;
					if (resdef == NULL)
						return PBSE_UNKRESC;
					else
						continue;
				} else
					return PBSE_BADATVAL;
			} else if (!(strcasecmp(name, ATTR_queue))) {
				*p = ch;
#ifndef PBS_MOM
				if (ch != '=')
					return PBSE_BADATVAL;
				p++;
				if (find_queuebyname(p) != NULL) {
					continue;
				} else {
					return PBSE_UNKQUE;
				}
#endif
			} else {
				*p = ch;
				return PBSE_BADATVAL;
			}
		} else {
			return PBSE_BADATVAL;
		}
	}
	return PBSE_NONE;
}

#ifndef PBS_MOM
/**
 * @brief
 * 		host_action - action routine for job's resource_list host resource
 *		validate the legality of the host name syntax
 *
 * @param[in]	presc    -       pointer to resource
 * @param[in]   pattr    -       not used here
 * @param[in]   pobj     -       not used here
 * @param[in]   type     -       not used here
 * @param[in]   actmode  -       mode of action routine
 *
 * @return	int
 * @retval	0	: success
 * @retval	PBSE_BADATVAL	: host name is not alpha numerical
 * @retval	PBSE_SYSTEM	: strdup failed, probably due to malloc failure
 */
int
host_action(resource *presc, attribute *pattr, void *pobj, int type, int actmode)
{
	char *name;
	extern char *resc_in_err;

	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return 0;

	name = presc->rs_value.at_val.at_str;
	if (name) {
		for (; *name; ++name) {
			if (isalnum((int) *name) ||
			    *name == '-' ||
			    *name == '_' ||
			    *name == '.') {
				continue;
			} else {
				if ((resc_in_err = strdup(presc->rs_value.at_val.at_str)) == NULL)
					return PBSE_SYSTEM;
				return PBSE_BADATVAL;
			}
		}
	}
	return 0;
}

/**
 * @brief
 *		Check select string for key
 * @par
 *		We can't just use strstr because it could match something with
 *		key as the last part of a longer string.  For example, looking
 *		for "eoe=" in "1:cooleoe=3" would match with strstr but would
 *		be a false positive.
 * @param[in]	str		string from select to search
 * @param[in]	key		string to search for
 *
 * @return	char*
 * @retval	NULL	key not found
 * @retval	!NULL	location of key
 */
char *
select_search(char *str, char *key)
{
	char *loc = strstr(str, key);
	char *prev;

	if (loc == NULL) /* not found at all */
		return NULL;
	if (str == loc) /* key is initial string */
		return loc;
	prev = loc - 1;			  /* look at char before key */
	if (*prev == ':' || *prev == '+') /* key is really there */
		return loc;
	return NULL; /* some other string like "1:xeoe=42" */
}

/**
 * @brief
 *      Wrapper routine for resc_select_action
 *
 * @par Functionality:
 *      It applies rules to validate eoe in the chunks. Rules are:
 *      (a) either all chunks request eoe or none request eoe
 *      (b) all chunks request same eoe
 *
 * @param[in]   presc    -       pointer to resource
 * @param[in]   pattr    -       pointer to attribute
 * @param[in]   pobj     -       pointer to job or reservation
 * @param[in]   type     -       if job or reservation
 *
 * @return		int
 * @retval		PBSE_NONE : success if no provisioning needed
 * @retval		PBSE_IVAL_AOECHUNK : if rules not met, the message for
 *						this EOE define is what is needed here as well
 * @retval		PBSE_SYSTEM : if error
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
static int
apply_eoe_inchunk_rules(resource *presc, attribute *pattr, void *pobj,
			int type)
{
	int c = 1, i; /* # of chunks, len of aoename */
	int ret = PBSE_NONE;
	static char key[] = "eoe=";
	char *name;
	char *peoe = NULL;    /* stores addr of eoe */
	char *eoename = NULL; /* 1st eoe found in select */
	char *tmpptr;	      /* store temp addr */

	if ((name = presc->rs_value.at_val.at_str) == NULL)
		return PBSE_NONE;

	/* eoe is requested? */
	if ((peoe = select_search(name, key)) == NULL)
		return PBSE_NONE;

	/* count # of chunks; ignore chunk multiplier */
	for (tmpptr = name; *tmpptr; tmpptr++)
		if (*tmpptr == '+')
			c++;

	/* find key ; reduce c each time pattern is found. */
	for (; peoe; c--, (peoe = select_search(peoe, key))) {
		/* point to eoe name. */
		peoe += strlen(key);
		tmpptr = peoe;
		/* get length of eoe name in i */
		for (i = 0; *tmpptr && *tmpptr != ':' && *tmpptr != '+';
		     i++, tmpptr++)
			;
		/* if first appearance of eoe, store it. */
		if (eoename == NULL) {
			eoename = malloc(i + 1);
			if (eoename == NULL)
				return PBSE_SYSTEM;
			strncpy(eoename, peoe, i);
			eoename[i] = '\0';
		}
		/* compare previously stored eoe and this eoe  */
		if (strncmp(peoe, eoename, i)) {
			ret = PBSE_IVAL_AOECHUNK; /* rule (b)*/
			break;
		}
	}
	/* there were chunks without eoe */
	if (c)
		ret = PBSE_IVAL_AOECHUNK; /* rule (a) */

	if (eoename)
		free(eoename);
	return ret;
}
/**
 * @brief
 *      Action routine for resource 'select'
 *
 * @param[in]   presc    -       pointer to resource
 * @param[in]   pattr    -       pointer to attribute
 * @param[in]   pobj     -       pointer to job or reservation
 * @param[in]   type     -       if job or reservation
 * @param[in]   actmode  -       mode of action routine
 *
 * @return	int
 * @retval	PBSE_NONE	: success if no provisioning needed
 * @retval	PBSE_IVAL_AOECHUNK	: if rules not met
 * @retval	PBSE_SYSTEM	: if error
 *
 * @par Side Effects:
 *		None
 *
 * @par	MT-safe: Yes
 *
 */

int
resc_select_action(resource *presc, attribute *pattr, void *pobj,
		   int type, int actmode)
{
	int rc = 0;
	if ((actmode != ATR_ACTION_NEW) && (actmode != ATR_ACTION_ALTER))
		return PBSE_NONE;
	rc = apply_select_inchunk_rules(presc, pattr, pobj, type, actmode);
	if (rc != PBSE_NONE)
		return rc;
	rc = apply_eoe_inchunk_rules(presc, pattr, pobj, type);
	if (rc != PBSE_NONE)
		return rc;

	/* not performing check if job is being created since reservation
	 * related data is not available yet.
	 */
	if (type == PARENT_TYPE_JOB && actmode == ATR_ACTION_NEW)
		return PBSE_NONE;

	return apply_aoe_inchunk_rules(presc, pattr, pobj, type);
}

/**
 * @brief
 *      Wrapper routine for resc_select_action
 *
 * @par Functionality:
 *      It applies rules to validate aoe in the chunks. Rules are:
 *      (a) either all chunks request aoe or none request aoe
 *      (b) all chunks request same aoe
 *      (c) job with aoe cannot be in reservation without aoe
 *      (d) job without aoe cannot be in reservation with aoe
 *      (e) reservation and job in it, have same aoe
 *
 * @param[in]   presc    -       pointer to resource
 * @param[in]   pattr    -       pointer to attribute
 * @param[in]   pobj     -       pointer to job or reservation
 * @param[in]   type     -       if job or reservation
 *
 * @return	int
 * @retval	PBSE_NONE	: success if no provisioning needed
 * @retval	PBSE_IVAL_AOECHUNK	: if rules not met
 * @retval	PBSE_SYSTEM	: if error
 *
 * @par Side Effects:
 *		None
 *
 * @par MT-safe: Yes
 *
 */
int
apply_aoe_inchunk_rules(resource *presc, attribute *pattr, void *pobj,
			int type)
{
	job *jb = NULL;
	int c = 1, i; /* # of chunks, len of aoename */
	char *name;
	char *paoe = NULL;    /* stores addr of aoe */
	char *aoename = NULL; /* 1st aoe found in select */
	char *tmpptr;	      /* store temp addr */
	char *aoe_req = NULL; /* Null if job outside reservation,
					 * Null if reservation has no aoe,
					 * not NULL if reservation has aoe */

	if (type == PARENT_TYPE_JOB) {
		jb = (job *) pobj;
		if (jb->ji_myResv) /* Get aoe requested by reservation */
			aoe_req = (char *) find_aoe_from_request(jb->ji_myResv);
	}

	name = presc->rs_value.at_val.at_str;
	if (name) {
		/* easy n quick check first: aoe is requested? */
		if ((paoe = strstr(name, "aoe=")) == NULL) {
			if (aoe_req) {
				free(aoe_req);
				return PBSE_IVAL_AOECHUNK; /* rule (d) */
			}
		} else {
			/* aoe is requested, slow down for checks */

			tmpptr = name;
			/* count # of chunks; ignore chunk multiplier */
			for (; *tmpptr; tmpptr++)
				if (*tmpptr == '+')
					c++;

			/* find pattern aoe= ; reduce c each time
			 * pattern is found. paoe was set in 'if' block
			 */
			for (; paoe; c--, (paoe = strstr(paoe, "aoe="))) {
				/* point to aoe name. */
				paoe += 4;
				tmpptr = paoe;
				/* get length of aoe name in i. */
				for (i = 0; *tmpptr && *tmpptr != ':' &&
					    *tmpptr != '+';
				     i++, tmpptr++)
					;
				/* if first appearance of aoe, store it. */
				if (aoename == NULL) {
					aoename = malloc(i + 1);
					if (aoename == NULL) {
						if (aoe_req)
							free(aoe_req);
						return PBSE_SYSTEM;
					}
					strncpy(aoename, paoe, i);
					aoename[i] = '\0';
				}
				/* compare previously stored aoe and
				 * this aoe.
				 */
				if (strncmp(paoe, aoename, i)) {
					if (aoe_req)
						free(aoe_req);
					if (aoename)
						free(aoename);
					return PBSE_IVAL_AOECHUNK; /* rule (b)*/
				}
				/* if job is in reservation, compare
				 * with reservation's aoe.
				 */
				if (type == PARENT_TYPE_JOB && jb->ji_myResv) {
					if (aoe_req == NULL ||
					    strncmp(aoe_req, aoename, i)) {
						if (aoe_req)
							free(aoe_req);
						if (aoename)
							free(aoename);
						/* rule (c/e) */
						return PBSE_IVAL_AOECHUNK;
					}
				}
			}
		}
	}

	if (aoe_req)
		free(aoe_req);
	if (aoename)
		free(aoename);
	return PBSE_NONE;
}
#endif /* not PBS_MOM */
/**
 * @brief
 *      action routine for built-in resources to check if its value is zero
 *      or positive whose datatype is long.
 *
 * @param[in]   presc	-	pointer to resource
 * @param[in]   pattr	-	pointer to attribute
 * @param[in]   pobj	-	pointer to job or reservation
 * @param[in]   type	-	if job or reservation
 * @param[in]   actmode	-	mode of action routine
 *
 * @return	int
 * @retval	PBSE_NONE	: success(if value is greater than or equal to zero)
 * @retval	PBSE_BADATVAL	: if value is less than zero
 *
 * @par Side Effects:
 *		None
 *
 * @par MT-safe: Yes
 *
 */
int
zero_or_positive_action(resource *presc, attribute *pattr, void *pobj, int type, int actmode)
{
	long l;
	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return 0;
	l = presc->rs_value.at_val.at_long;
	if (l < 0)
		return PBSE_BADATVAL;
	return PBSE_NONE;
}
/**
 * @brief
 *      Wrapper routine for resc_select_action
 *      It applies rules to validate all individual resources in all the chunks.
 *
 * @par Functionality:
 *      1. Parses select specification by calling parse_chunk function.
 *      2. Decodes each chunk
 *      3. Calls resource action function for each resource in a chunk if
 *	   the resource is of type long.
 *
 * @param[in]   presc	-	pointer to resource
 * @param[in]   pattr	-	pointer to attribute
 * @param[in]   pobj	-	pointer to job
 * @param[in]   type	-	if job or reservation
 * @param[in]   actmode	-	mode of action routine
 *
 * @return	int
 * @retval	PBSE_NONE	: success
 * @retval	> 0	: if error
 *
 * @par Side Effects:
 *		None
 *
 */
int
apply_select_inchunk_rules(resource *presc, attribute *pattr, void *pobj, int type, int actmode)
{
	char *chunk;
	int nchk;
	int nelem;
	struct key_value_pair *pkvp;
	int rc = 0;
	int j;
	struct resource tmp_resc;
	char *select_str = NULL;

	select_str = presc->rs_value.at_val.at_str;
	if ((select_str == NULL) || (select_str[0] == '\0'))
		return PBSE_BADATVAL;
	chunk = parse_plus_spec(select_str, &rc); /* break '+' seperated substrings */
	if (rc != 0)
		return (rc);
	while (chunk) {
		if (parse_chunk(chunk, &nchk, &nelem, &pkvp, NULL) == 0) {
			for (j = 0; j < nelem; ++j) {
				tmp_resc.rs_defin = find_resc_def(svr_resc_def, pkvp[j].kv_keyw);
				if ((tmp_resc.rs_defin != NULL) && (tmp_resc.rs_defin->rs_type == ATR_TYPE_LONG)) {
					tmp_resc.rs_value.at_val.at_long = atol(pkvp[j].kv_val);
					if (tmp_resc.rs_defin->rs_action) {
						if ((rc = tmp_resc.rs_defin->rs_action(&tmp_resc, pattr, pobj,
										       type, actmode)) != 0)
							return (rc);
					}
				}
			}
		} else {
			return PBSE_BADATVAL;
		}
		chunk = parse_plus_spec(NULL, &rc);
		if (rc != 0)
			return (rc);
	} /* while */
	return PBSE_NONE;
}
/**
 * @brief action_soft_walltime - action function for the soft_walltime resource.
 *
 * 	returns int
 * 	@retval PBSE_BADATVAL - soft_walltime > walltime
 * 	@retval PBSE_SOFTWT_STF - min_walltime is set
 * 	@retval PBSE_NONE - everything is fine
 */
int
action_soft_walltime(resource *presc, attribute *pattr, void *pobject, int type, int actmode)
{
	job *pjob;

	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return PBSE_NONE;

	if (pobject != NULL) {
		static resource_def *walltime_def = NULL;
		static resource_def *min_walltime_def = NULL;
		resource *entry;

		if (type != PARENT_TYPE_JOB)
			return PBSE_NONE;

		pjob = (job *) pobject;

		/* Make sure soft_walltime < walltime */
		if (walltime_def == NULL)
			walltime_def = &svr_resc_def[RESC_WALLTIME];
		entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), walltime_def);
		if (entry != NULL) {
			if (is_attr_set(&entry->rs_value)) {
				if (walltime_def->rs_comp(&(entry->rs_value), &(presc->rs_value)) < 0)
					return PBSE_BADATVAL;
			}
		}

		/* soft_walltime and STF jobs are incompatible */
		if (min_walltime_def == NULL)
			min_walltime_def = &svr_resc_def[RESC_MIN_WALLTIME];
		entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), min_walltime_def);
		if (entry != NULL) {
			if (is_attr_set(&entry->rs_value))
				return PBSE_SOFTWT_STF;
		}
	}
	return PBSE_NONE;
}
/**
 * @brief action_walltime - action function for the soft_walltime resource.
 *
 * 	returns int
 * 	@retval PBSE_BADATVAL - walltime < soft_walltime
 * 	@retval PBSE_NONE - everything is fine
 */

int
action_walltime(resource *presc, attribute *pattr, void *pobject, int type, int actmode)
{
	job *pjob;
	resource *entry;

	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return PBSE_NONE;

	if (pobject != NULL) {
		static resource_def *soft_walltime_def = NULL;

		if (type != PARENT_TYPE_JOB)
			return PBSE_NONE;

		pjob = (job *) pobject;

		/* Make sure walltime > soft_walltime */
		if (soft_walltime_def == NULL)
			soft_walltime_def = &svr_resc_def[RESC_SOFT_WALLTIME];
		entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), soft_walltime_def);
		if (entry != NULL) {
			if (is_attr_set(&entry->rs_value)) {
				if (soft_walltime_def->rs_comp(&(entry->rs_value), &(presc->rs_value)) > 0)
					return PBSE_BADATVAL;
			}
		}
	}
	return PBSE_NONE;
}

/**
 * @brief action_min_walltime - action function for min_walltime.
 * @return int
 * @retval PBSE_NOSTF_JOBARRAY - if min_walltime is on a job array
 * @retval PBSE_SOFTWT_STF - if min_walltime is set with soft_walltime
 * @retval PBSE_MIN_GT_MAXWT - if min_walltime > max_walltime
 * @retval PBSE_NONE - all is fine
 */
int
action_min_walltime(resource *presc, attribute *pattr, void *pobject, int type, int actmode)
{
	job *pjob;

	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return PBSE_NONE;

	if (pobject != NULL) {
		static resource_def *soft_walltime_def = NULL;
		static resource_def *max_walltime_def = NULL;
		resource *entry;

		if (type != PARENT_TYPE_JOB)
			return PBSE_NONE;

		pjob = (job *) pobject;

#ifndef PBS_MOM /* MOM doesn't call the action functions and doesn't have access to is_job_array() */
		/* Job arrays can't be STF jobs */
		if (is_job_array(pjob->ji_qs.ji_jobid) != IS_ARRAY_NO)
			return PBSE_NOSTF_JOBARRAY;
#endif
		/* STF jobs can't request soft_walltime */
		if (soft_walltime_def == NULL)
			soft_walltime_def = &svr_resc_def[RESC_SOFT_WALLTIME];
		if (soft_walltime_def != NULL) {
			entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), soft_walltime_def);
			if (entry != NULL) {
				if (is_attr_set(&entry->rs_value))
					return PBSE_SOFTWT_STF;
			}
		}

		/* max_walltime needs to be greater than min_walltime */
		if (max_walltime_def == NULL)
			max_walltime_def = &svr_resc_def[RESC_MAX_WALLTIME];
		if (max_walltime_def != NULL) {
			entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), max_walltime_def);
			if (entry != NULL && (is_attr_set(&entry->rs_value)))
				if (max_walltime_def->rs_comp(&(entry->rs_value), &(presc->rs_value)) < 0)
					return PBSE_MIN_GT_MAXWT;
		}
	}
	return PBSE_NONE;
}

/**
 * @brief action_max_walltime - action function for max_walltime.
 * @return int
 * @retval PBSE_SOFTWT_STF - if max_walltime is set with soft_walltime
 * @retval PBSE_MIN_GT_MAXWT - if max_walltime < min_walltime
 * @retval PBSE_MAX_NO_MINWT - max_walltime with no min_walltime
 * @retval PBSE_NONE - all is fine
 */

int
action_max_walltime(resource *presc, attribute *pattr, void *pobj, int type, int actmode)
{
	job *pjob;

	if ((actmode != ATR_ACTION_ALTER) && (actmode != ATR_ACTION_NEW))
		return PBSE_NONE;

	if (pobj != NULL) {
		static resource_def *soft_walltime_def = NULL;
		static resource_def *min_walltime_def = NULL;
		resource *entry;

		if (type != PARENT_TYPE_JOB)
			return PBSE_NONE;

		pjob = (job *) pobj;

		/* STF jobs can't request soft_walltime */
		if (soft_walltime_def == NULL)
			soft_walltime_def = &svr_resc_def[RESC_SOFT_WALLTIME];
		if (soft_walltime_def != NULL) {
			entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), soft_walltime_def);
			if (entry != NULL) {
				if (is_attr_set(&entry->rs_value))
					return PBSE_SOFTWT_STF;
			}
		}

		/* max_walltime needs to be greater than min_walltime */
		if (min_walltime_def == NULL)
			min_walltime_def = &svr_resc_def[RESC_MIN_WALLTIME];
		if (min_walltime_def != NULL) {
			entry = find_resc_entry(get_jattr(pjob, JOB_ATR_resource), min_walltime_def);
			if (entry != NULL) {
				if (is_attr_set(&entry->rs_value)) {
					if (min_walltime_def->rs_comp(&(entry->rs_value), &(presc->rs_value)) > 0)
						return PBSE_MIN_GT_MAXWT;
				}
			} else
				return PBSE_MAX_NO_MINWT;
		}
	}
	return PBSE_NONE;
}
