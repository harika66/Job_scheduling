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
 * @file	work_task.c
 * @brief
 * work_task.c - contains functions to deal with the server's task list
 */
#include <pbs_config.h> /* the master config generated by configure */

#include "portability.h"
#include <stdlib.h>
#include <time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"

/* Global Data Items: */

extern pbs_list_head task_list_immed;	   /* list of tasks that can execute now */
extern pbs_list_head task_list_interleave; /* list of tasks that can execute after interleaving other tasks */
extern pbs_list_head task_list_timed;	   /* list of tasks that have set start times */
extern pbs_list_head task_list_event;	   /* list of tasks responding to an event */
extern int svr_delay_entry;
extern time_t time_now;

/**
 *
 * @brief
 * 	Creates a task of type 'type', 'event_id', and when task is dispatched,
 *	execute func with argument 'parm'. The task is added to
 *	'task_list_immed' if 'type' is  WORK_Immed; otherwise, task is added
 *	'task_list_event'.
 *
 * @param[in]	type - of task
 * @param[in]	event_id - event id of the task
 * @param[in]	func - function that will be executed in behalf of the task
 * @param[in]	parm - parameter to 'func'
 *
 * @return struct work_task *
 * @retval <a work task entry>	- for success
 * @retval NULL			- for any error
 *
 */
struct work_task *
set_task(enum work_type type, long event_id, void (*func)(struct work_task *), void *parm)
{
	struct work_task *pnew;
	struct work_task *pold;

	pnew = (struct work_task *) malloc(sizeof(struct work_task));
	if (pnew == NULL)
		return NULL;
	CLEAR_LINK(pnew->wt_linkevent);
	CLEAR_LINK(pnew->wt_linkobj);
	CLEAR_LINK(pnew->wt_linkobj2);
	pnew->wt_event = event_id;
	pnew->wt_event2 = NULL;
	pnew->wt_type = type;
	pnew->wt_func = func;
	pnew->wt_parm1 = parm;
	pnew->wt_parm2 = NULL;
	pnew->wt_parm3 = NULL;
	pnew->wt_aux = 0;
	pnew->wt_aux2 = 0;

	if (type == WORK_Immed)
		append_link(&task_list_immed, &pnew->wt_linkevent, pnew);
	else if (type == WORK_Interleave)
		append_link(&task_list_interleave, &pnew->wt_linkevent, pnew);
	else if (type == WORK_Timed) {
		pold = (struct work_task *) GET_NEXT(task_list_timed);
		while (pold) {
			if (pold->wt_event > pnew->wt_event)
				break;
			pold = (struct work_task *) GET_NEXT(pold->wt_linkevent);
		}
		if (pold)
			insert_link(&pold->wt_linkevent, &pnew->wt_linkevent, pnew,
				    LINK_INSET_BEFORE);
		else
			append_link(&task_list_timed, &pnew->wt_linkevent, pnew);
	} else
		append_link(&task_list_event, &pnew->wt_linkevent, pnew);
	return (pnew);
}

/**
 *
 * @brief
 * 	Convert a work task to the type specified.
 *
 * @param[in]	ptask	- task being converted
 * @param[in]	wtype	- work task type to convert
 *
 * @return int
 * @retval 0: success
 * @retval -1: failure
 */
int
convert_work_task(struct work_task *ptask, enum work_type wtype)
{
	pbs_list_head *list;

	if (!ptask)
		return -1;

	switch (wtype) {
		case WORK_Immed:
			list = &task_list_immed;
			break;
		case WORK_Timed:
			list = &task_list_timed;
			break;
		default:
			list = &task_list_event;
	}

	delete_link(&ptask->wt_linkevent);
	append_link(list, &ptask->wt_linkevent, ptask);

	return 0;
}

/**
 *
 * @brief
 * 	Dispatches a work task found on a work list
 *
 * @param[in]	ptask	- task being dispatched
 *
 * @note:
 *	This also deletes the work task entry, calls the associated function
 *	with the parameters from the work task entry, and then frees the
 *	entry.
 */

void
dispatch_task(struct work_task *ptask)
{
	delete_link(&ptask->wt_linkevent);
	delete_link(&ptask->wt_linkobj);
	delete_link(&ptask->wt_linkobj2);
	if (ptask->wt_func)
		ptask->wt_func(ptask); /* dispatch process function */
	(void) free(ptask);
}

/**
 *
 * @brief
 * 	Unlinks and frees a work_task structure.
 *
 * @param[in]	ptask	- the task entry being deleted.
 */

void
delete_task(struct work_task *ptask)
{
	delete_link(&ptask->wt_linkobj);
	delete_link(&ptask->wt_linkobj2);
	delete_link(&ptask->wt_linkevent);
	(void) free(ptask);
}

/**
 * @brief
 *	Check if some task in the specified task list
 *	has a wt_parm1 matching 'parm1'
 *	and wt_func matching 'func'
 *
 * @param[in]	task_list - header of task list to be searched
 * @param[in]	parm1	- parameter being matched.
 * @param[in]	func	- function being matched.
 *
 * @return work task
 * @retval	!NULL if 'parm1' and 'func' was matched
 * @retval	NULL otherwise
 */
static struct work_task *
find_worktask_by_parm_func(pbs_list_head task_list, void *parm1, void *func)
{
	struct work_task *ptask;
	struct work_task *ptask_next;

	for (ptask = GET_NEXT(task_list); ptask; ptask = ptask_next) {
		ptask_next = GET_NEXT(ptask->wt_linkevent);

		if (parm1 && (ptask->wt_parm1 != parm1))
			continue;
		if (func && (ptask->wt_func != func))
			continue;

		return ptask;
	}

	return NULL;
}

/**
 * @brief
 *	Check if some task in in any of the task lists (task_list_event,
 *	task_list_timed, task_list_immed)
 *	has a wt_parm1 matching 'parm1'
 *	and wt_func matching 'func'
 *
 * @param[in]	wtype - work task type enum, -1 to match all
 * @param[in]	parm1	- parameter being matched. NULL to ignore this field.
 * @param[in]	func	- function being matched. NULL to ignore this field.
 *
 * @return work task
 * @retval	!NULL if 'parm1' and 'func' was matched
 * @retval	NULL otherwise
 */
struct work_task *
find_work_task(enum work_type wtype, void *parm1, void *func)
{
	struct work_task *ptask;

	if (wtype == -1 || wtype == WORK_Immed) {
		ptask = find_worktask_by_parm_func(task_list_immed, parm1, func);
		if (ptask)
			return ptask;
	}

	if (wtype == -1 || wtype == WORK_Timed) {
		ptask = find_worktask_by_parm_func(task_list_timed, parm1, func);
		if (ptask)
			return ptask;
	}

	if (wtype == -1 || (wtype != WORK_Timed && wtype != WORK_Immed)) {
		ptask = find_worktask_by_parm_func(task_list_event, parm1, func);
		if (ptask)
			return ptask;
	}

	return NULL;
}

/**
 *
 * @brief
 *	Delete task found in task_list_event, task_list_immed, or
 *	task_list_timed by either its function pointer, parm1, or both.
 * 	At least one of the function pointer or parm1 must not be NULL.
 *
 * @param[in]	parm1	- wt->parm1 parameter to match (can be NULL)
 * @param[in]	func	- function pointer to match (can be NULL)
 * @param[in]	option  - option is used to decide whether the
 *			  caller wants to delete all tasks that
 *			  matches parm1 values or just one.
 *
 * @return none
 */
void
delete_task_by_parm1_func(void *parm1, void (*func)(struct work_task *), enum wtask_delete_option option)
{
	struct work_task *ptask;
	struct work_task *ptask_next;
	pbs_list_head task_lists[] = {task_list_event, task_list_timed, task_list_immed};
	int i;

	if (parm1 == NULL && func == NULL)
		return;

	for (i = 0; i < 3; i++) {
		for (ptask = (struct work_task *) GET_NEXT(task_lists[i]); ptask; ptask = ptask_next) {
			ptask_next = (struct work_task *) GET_NEXT(ptask->wt_linkevent);

			if ((parm1 != NULL) && (ptask->wt_parm1 != parm1))
				continue;
			if ((func != NULL) && (ptask->wt_func != func))
				continue;

			delete_task(ptask);
			if (option == DELETE_ONE)
				return;
		}
	}
}

/**
 *
 * @brief
 *	Check if some task in any of the task lists (task_list_event,
 *	task_list_timed, task_list_immed) has a wt_parm1 matching 'parm1'.
 *
 * @param[in]	parm1	- parameter being matched.
 *
 * @return int
 * @retval	1 if 'parm1' was matched
 * @retval	0 otherwise
 */
int
has_task_by_parm1(void *parm1)
{
	struct work_task *ptask;

	/* only 1 ptask can be possibly matched */
	ptask = find_work_task(-1, parm1, NULL);
	if (ptask)
		return 1;

	return 0;
}

/**
 * @brief
 *	Looks for the next work task to perform:
 *	1. If svr_delay_entry is set, then a delayed task in the
 *	   task_list_event is ready so find and process it.
 *	2. All items on the immediate list, then
 *	3. All items on the timed task list which have expired times
 *
 * @return time_t
 * @retval The amount of time till next task
 */

time_t
default_next_task(void)
{
	time_t delay;
	struct work_task *nxt;
	struct work_task *ptask;
	struct work_task *last_interleave_task;
	/*
	 * tilwhen is the basic "idle" time if there is nothing pending sooner
	 * for the Server (timed-events, call scheduler, IO)
	 * It used to be 10, but that caused a delay of outgoing TPP packets
	 * in some cases, and we don't burn too many extra cycles doing nothing
	 * if the delay is shorted to 2.
	 */
	time_t tilwhen = 2; /* basic cycle time */

	time_now = time(NULL);

	if (svr_delay_entry) {
		ptask = (struct work_task *) GET_NEXT(task_list_event);
		while (ptask) {
			nxt = (struct work_task *) GET_NEXT(ptask->wt_linkevent);
			if (ptask->wt_type == WORK_Deferred_Cmp)
				dispatch_task(ptask);
			ptask = nxt;
		}
		svr_delay_entry = 0;
	}

	while ((ptask = (struct work_task *) GET_NEXT(task_list_immed)) != NULL)
		dispatch_task(ptask);

	last_interleave_task = (struct work_task *) GET_PRIOR(task_list_interleave);
	while ((ptask = (struct work_task *) GET_NEXT(task_list_interleave)) != NULL) {
		dispatch_task(ptask);
		if (ptask == last_interleave_task)
			break;
	}

	if (GET_NEXT(task_list_interleave)) {
		/* more tasks waiting, wait least */
		tilwhen = 0;
	}

	while ((ptask = (struct work_task *) GET_NEXT(task_list_timed)) != NULL) {
		if ((delay = ptask->wt_event - time_now) > 0) {
			if (tilwhen > delay)
				tilwhen = delay;
			break;
		} else {
			dispatch_task(ptask); /* will delete link */
		}
	}

	return (tilwhen);
}
