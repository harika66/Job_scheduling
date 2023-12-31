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
 * @file	req_shutdown.c
 *
 * @brief
 * 		req_shutdown.c - contains the functions to shutdown the server
 *
 * Included functions are:
 * 	svr_shutdown()
 * 	shutdown_ack()
 * 	req_shutdown()
 * 	shutdown_preempt_chkpt()
 * 	post_chkpt()
 * 	rerun_or_kill()
 *
 */
#include <pbs_config.h> /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>
#include "libpbs.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "log.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "pbs_error.h"
#include "sched_cmds.h"
#include "acct.h"
#include "pbs_nodes.h"
#include "svrfunc.h"

/* Private Fuctions Local to this File */

int shutdown_preempt_chkpt(job *);
void post_hold(struct work_task *);
static void post_chkpt(struct work_task *);
static void rerun_or_kill(job *, char *text);

/* Private Data Items */

static struct batch_request *pshutdown_request = 0;

/* Global Data Items: */

extern pbs_list_head svr_alljobs;
extern char *msg_abort_on_shutdown;
extern char *msg_daemonname;
extern char *msg_init_queued;
extern char *msg_shutdown_op;
extern char *msg_shutdown_start;
extern char *msg_leftrunning;
extern char *msg_stillrunning;
extern char *msg_on_shutdown;
extern char *msg_job_abort;

extern pbs_list_head task_list_event;
extern struct server server;
extern attribute_def svr_attr_def[];

/**
 * @brief
 *		Perform start of the shutdown procedure for the server
 *
 * @par failover
 *		In failover environment, may need to tell Secondary to either stay
 *		inactive, to shutdown (only the Secondary) or to shutdown as well.
 *		In the cases the Primary is also going down, we want to wait
 *		for an acknowledgement from Secondary.  That is down by or-ing in
 *		SV_STATE_PRIMDLY to the server internal state.   see failover.c and
 *		the main processing loop in pbsd_main.c;  that loop won't exit if
 *		SV_STATE_PRIMDLY is in the state.
 *
 * @param[in]	type	-	SHUT_* - type of for shutdown, see pbs_internal.h
 */

void
svr_shutdown(int type)
{
	job *pjob;
	job *pnxt;
	long state;
	int wait_for_secondary = 0;

	/* Lets start by logging shutdown and saving everything */

	/* Saving server jobid number to the database as server is going to shutdown.
	 * Once server will come up then it will start jobid/resvid from this number onwards.
	 */
	state = get_sattr_long(SVR_ATR_State);
	(void) strcpy(log_buffer, msg_shutdown_start);

	if (state == SV_STATE_SHUTIMM) {

		/* if already shuting down, another Immed/sig will force it */

		if ((type == SHUT_IMMEDIATE) || (type == SHUT_SIG)) {
			state = SV_STATE_DOWN;
			set_sattr_l_slim(SVR_ATR_State, state, SET);
			(void) strcat(log_buffer, "Forced");
			log_event(PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
				  PBS_EVENTCLASS_SERVER, LOG_NOTICE,
				  msg_daemonname, log_buffer);
			return;
		}
	}

	/* in failover environments, need to communicate with Secondary */
	/* and for these two where the Primary is going down, mark to   */
	/* wait for the acknowledgement from the Secondary              */

	if (type & SHUT_WHO_SECDRY) {
		if (failover_send_shutdown(FAILOVER_SecdShutdown) == 0)
			wait_for_secondary = 1;
	} else if (type & SHUT_WHO_IDLESECDRY) {
		if (failover_send_shutdown(FAILOVER_SecdGoInactive) == 0)
			wait_for_secondary = 1;
	}

	/* what is the manner of our demise? */

	type = type & SHUT_MASK;
	if (type == SHUT_IMMEDIATE) {
		state = SV_STATE_SHUTIMM;
		set_sattr_l_slim(SVR_ATR_State, state, SET);
		(void) strcat(log_buffer, "Immediate");

	} else if (type == SHUT_DELAY) {
		state = SV_STATE_SHUTDEL;
		set_sattr_l_slim(SVR_ATR_State, state, SET);
		(void) strcat(log_buffer, "Delayed");

	} else if (type == SHUT_QUICK) {
		state = SV_STATE_DOWN; /* set to down to brk pbsd_main loop */
		set_sattr_l_slim(SVR_ATR_State, state, SET);
		(void) strcat(log_buffer, "Quick");

	} else {
		state = SV_STATE_DOWN;
		set_sattr_l_slim(SVR_ATR_State, state, SET);
		(void) strcat(log_buffer, "By Signal");
		type = SHUT_QUICK;
	}
	log_event(PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
		  PBS_EVENTCLASS_SERVER, LOG_NOTICE, msg_daemonname, log_buffer);

	if (wait_for_secondary) {
		state |= SV_STATE_PRIMDLY; /* wait for reply from Secondary */
		set_sattr_l_slim(SVR_ATR_State, state, SET);
	}

	if (type == SHUT_QUICK) /* quick, leave jobs as are */
		return;
	svr_save_db(&server);

	pnxt = (job *) GET_NEXT(svr_alljobs);
	while ((pjob = pnxt) != NULL) {
		pnxt = (job *) GET_NEXT(pjob->ji_alljobs);

		if (check_job_state(pjob, JOB_STATE_LTR_RUNNING)) {
			char *val = get_jattr_str(pjob, JOB_ATR_chkpnt);

			pjob->ji_qs.ji_svrflags |= JOB_SVFLG_HOTSTART;
			pjob->ji_qs.ji_svrflags |= JOB_SVFLG_HASRUN;
			if (val && (*val != 'n')) {
				/* do checkpoint of job */

				if (shutdown_preempt_chkpt(pjob) == 0)
					continue;
			}

			/* if not checkpoint (not supported, not allowed, or fails */
			/* rerun if possible, else kill job			   */

			rerun_or_kill(pjob, msg_on_shutdown);
		}
	}
	return;
}

/**
 * @brief
 * 		shutdown_ack - acknowledge the shutdown (terminate) request
 * 		if there is one.  This is about the last thing the server does
 *		before going away.
 */

void
shutdown_ack()
{
	if (pshutdown_request) {
		reply_ack(pshutdown_request);
		pshutdown_request = 0;
	}
}

/**
 * @brief
 * 		req_shutdown - process request to shutdown the server.
 * @note
 *		Must have operator or administrator privilege.
 *
 * @param[in,out]	preq	-	Shutdown Job Request
 */

void
req_shutdown(struct batch_request *preq)
{
	int type;
	extern int shutdown_who;

	if ((preq->rq_perm & (ATR_DFLAG_MGWR | ATR_DFLAG_MGRD | ATR_DFLAG_OPRD |
			      ATR_DFLAG_OPWR)) == 0) {
		req_reject(PBSE_PERM, 0, preq);
		return;
	}

	(void) sprintf(log_buffer, msg_shutdown_op, preq->rq_user, preq->rq_host);
	log_event(PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
		  PBS_EVENTCLASS_SERVER, LOG_NOTICE, msg_daemonname, log_buffer);

	pshutdown_request = preq; /* save for reply from main() when done */
	type = preq->rq_ind.rq_shutdown;
	shutdown_who = type & SHUT_WHO_MASK;

	if (shutdown_who & SHUT_WHO_SECDONLY)
		(void) failover_send_shutdown(FAILOVER_SecdShutdown);

	if (shutdown_who & SHUT_WHO_SCHED)
		send_sched_cmd(dflt_scheduler, SCH_QUIT, NULL); /* tell scheduler to quit */

	if (shutdown_who & SHUT_WHO_SECDONLY) {
		reply_ack(preq);
		return; /* do NOT shutdown this Server */
	}

	/* Moms are told to shutdown in pbsd_main.c after main loop */

	svr_shutdown(type);
	return;
}

/**
 * @brief
 * 		shutdown_preempt_chkpt - perform checkpoint of job by issuing a hold request to mom
 *
 * @param[in,out]	pjob	-	pointer to job
 * @param[in]		nest	-	pointer to the nested batch_request (if any)
 *
 * @return	int
 * @retval	0	: success
 * @retval	-1	: relay_to_mom failed
 * @retval	PBSE_SYSTEM	: error
 */

int
shutdown_preempt_chkpt(job *pjob)
{
	struct batch_request *phold;
	attribute temp;
	void (*func)(struct work_task *);

	long *hold_val = NULL;
	long old_hold = 0;

	phold = alloc_br(PBS_BATCH_HoldJob);
	if (phold == NULL)
		return (PBSE_SYSTEM);

	temp.at_flags = ATR_VFLAG_SET;
	temp.at_type = job_attr_def[(int) JOB_ATR_hold].at_type;
	temp.at_user_encoded = NULL;
	temp.at_priv_encoded = NULL;
	temp.at_val.at_long = HOLD_s;

	phold->rq_perm = ATR_DFLAG_MGRD | ATR_DFLAG_MGWR;
	(void) strcpy(phold->rq_ind.rq_hold.rq_orig.rq_objname, pjob->ji_qs.ji_jobid);
	CLEAR_HEAD(phold->rq_ind.rq_hold.rq_orig.rq_attr);
	if (job_attr_def[(int) JOB_ATR_hold].at_encode(&temp,
						       &phold->rq_ind.rq_hold.rq_orig.rq_attr,
						       job_attr_def[(int) JOB_ATR_hold].at_name,
						       NULL,
						       ATR_ENCODE_CLIENT, NULL) < 0)
		return (PBSE_SYSTEM);

	phold->rq_extra = pjob;
	func = post_chkpt;

	if (relay_to_mom(pjob, phold, func) == 0) {

		if (check_job_state(pjob, JOB_STATE_LTR_TRANSIT))
			svr_setjobstate(pjob, JOB_STATE_LTR_RUNNING, JOB_SUBSTATE_RUNNING);
		pjob->ji_qs.ji_svrflags |= (JOB_SVFLG_HASRUN | JOB_SVFLG_CHKPT | JOB_SVFLG_HASHOLD);
		(void) job_save_db(pjob);
		return (0);
	} else {
		*hold_val = old_hold; /* reset to the old value */
		return (-1);
	}
}

/**
 * @brief
 * 		post-chkpt - clean up after shutdown_preempt_chkpt
 *		This is called on the reply from MOM to a Hold request made in
 *		shutdown_preempt_chkpt().  If the request succeeded, then record in job.
 *		If the request failed, then we fall back to rerunning or aborting
 *		the job.
 *
 * @param[in]	ptask	-	work_task which contains the request
 */

static void
post_chkpt(struct work_task *ptask)
{
	job *pjob;
	struct batch_request *preq;

	preq = (struct batch_request *) ptask->wt_parm1;
	pjob = find_job(preq->rq_ind.rq_hold.rq_orig.rq_objname);
	if (!preq || !pjob)
		return;
	if (preq->rq_reply.brp_code == 0) {
		/* checkpointed ok */
		if (preq->rq_reply.brp_auxcode) { /* chkpt can be moved */
			pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_CHKPT;
			pjob->ji_qs.ji_svrflags |= JOB_SVFLG_ChkptMig;
			job_save_db(pjob);
		}
		account_record(PBS_ACCT_CHKPNT, pjob, NULL);
	} else {
		/* need to try rerun if possible or just abort the job */
		if (preq->rq_reply.brp_code != PBSE_CKPBSY) {
			pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_CHKPT;
			set_job_substate(pjob, JOB_SUBSTATE_RUNNING);
			job_save_db(pjob);
			if (check_job_state(pjob, JOB_STATE_LTR_RUNNING))
				rerun_or_kill(pjob, msg_on_shutdown);
		}
	}

	release_req(ptask);
}
/**
 * @brief
 * 	rerun_or_kill - rerun or kill a job and log the message
 *
 * 	@par Functionality:
 * 		If the job is re-runnable mark it to be re-queued.
 * 		If the job is not re-runnable, immediately kill the job.
 * 		Record log message before purging job.
 * 		If shutdown takes time, leave the job running.
 *
 * @param[in]	pjob	-	job which needs to be killed or re-runned.
 * @param[in]	text	-	message to be logged before purging the job.
 */
static void
rerun_or_kill(job *pjob, char *text)
{
	long server_state = get_sattr_long(SVR_ATR_State);

	if (get_jattr_long(pjob, JOB_ATR_rerunable)) {

		/* job is rerunable, mark it to be requeued */

		(void) issue_signal(pjob, "SIGKILL", release_req, 0);
		set_job_substate(pjob, JOB_SUBSTATE_RERUN);
		(void) strcpy(log_buffer, msg_init_queued);
		(void) strcat(log_buffer, pjob->ji_qhdr->qu_qs.qu_name);
		(void) strcat(log_buffer, text);
	} else if (server_state != SV_STATE_SHUTDEL) {

		/* job not rerunable, immediate shutdown - kill it off */

		(void) strcpy(log_buffer, msg_job_abort);
		(void) strcat(log_buffer, text);
		/* need to record log message before purging job */
		log_event(PBSEVENT_SYSTEM | PBSEVENT_JOB | PBSEVENT_DEBUG,
			  PBS_EVENTCLASS_JOB, LOG_INFO, pjob->ji_qs.ji_jobid,
			  log_buffer);
		(void) job_abt(pjob, log_buffer);
		return;
	} else {

		/* delayed shutdown, leave job running */

		(void) strcpy(log_buffer, msg_leftrunning);
		(void) strcat(log_buffer, text);
	}
	log_event(PBSEVENT_SYSTEM | PBSEVENT_JOB | PBSEVENT_DEBUG,
		  PBS_EVENTCLASS_JOB, LOG_NOTICE, pjob->ji_qs.ji_jobid,
		  log_buffer);
}
