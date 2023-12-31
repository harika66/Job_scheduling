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
 * @file    svr_recov_db.c
 *
 * @brief
 * 		svr_recov_db.c - contains functions to save server state and recover
 *
 */

#include <pbs_config.h> /* the master config generated by configure */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "pbs_ifl.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "server.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "log.h"
#include "pbs_db.h"
#include "pbs_sched.h"
#include "pbs_share.h"

/* Global Data Items: */

extern struct server server;
extern pbs_list_head svr_queues;
extern attribute_def svr_attr_def[];
extern char *path_priv;
extern time_t time_now;
extern char *msg_svdbopen;
extern char *msg_svdbnosv;
extern char *path_svrlive;
extern void *svr_db_conn;
extern void sched_free(pbs_sched *psched);

extern pbs_sched *sched_alloc(char *sched_name);

/**
 * @brief
 *		Update the $PBS_HOME/server_priv/svrlive file timestamp
 *
 * @return	Error code
 * @retval	0	: Success
 * @retval	-1	: Failed to update timestamp
 *
 */
int
update_svrlive()
{
	static int fdlive = -1;
	if (fdlive == -1) {
		/* first time open the file */
		fdlive = open(path_svrlive, O_WRONLY | O_CREAT, 0600);
		if (fdlive < 0)
			return -1;
	}
	(void) utimes(path_svrlive, NULL);
	return 0;
}

/**
 * @brief
 *	convert server structure to DB format
 *
 * @param[in]	ps	-	Address of the server in pbs server
 * @param[out]	pdbsvr	-	Address of the database server object
 *
 * @retval   -1  Failure
 * @retval	>=0 What to save: 0=nothing, OBJ_SAVE_NEW or OBJ_SAVE_QS
 *
 */
static int
svr_to_db(struct server *ps, pbs_db_svr_info_t *pdbsvr)
{
	int savetype = 0;

	pdbsvr->sv_jobidnumber = ps->sv_qs.sv_lastid;

	if ((encode_attr_db(svr_attr_def, ps->sv_attr, (int) SVR_ATR_LAST, &pdbsvr->db_attr_list, 1)) != 0) /* encode all attributes */
		return -1;

	if (ps->newobj) /* object was never saved or loaded before */
		savetype |= (OBJ_SAVE_NEW | OBJ_SAVE_QS);

	return savetype;
}

/**
 * @brief
 *	convert from DB to server structure
 *
 * @param[out]	ps	-	Address of the server in pbs server
 * @param[in]	pdbsvr	-	Address of the database server object
 *
 * @return   !=0   - Failure
 * @return   0     - Success
 */
int
db_to_svr(struct server *ps, pbs_db_svr_info_t *pdbsvr)
{
	if ((decode_attr_db(ps, &pdbsvr->db_attr_list.attrs, svr_attr_idx, svr_attr_def, ps->sv_attr, SVR_ATR_LAST, 0)) != 0)
		return -1;

	ps->newobj = 0;
	ps->sv_qs.sv_jobidnumber = pdbsvr->sv_jobidnumber;

	return 0;
}

/**
 * @brief
 *	convert sched structure to DB format
 *
 * @param[in]	ps - Address of the scheduler in pbs server
 * @param[out]  pdbsched  - Address of the database scheduler object
 *
 * @retval   -1  Failure
 * @retval	>=0 What to save: 0=nothing, OBJ_SAVE_NEW or OBJ_SAVE_QS
 */
static int
sched_to_db(struct pbs_sched *ps, pbs_db_sched_info_t *pdbsched)
{
	int savetype = 0;

	strcpy(pdbsched->sched_name, ps->sc_name);

	if ((encode_attr_db(sched_attr_def, ps->sch_attr, (int) SCHED_ATR_LAST, &pdbsched->db_attr_list, 0)) != 0)
		return -1;

	if (ps->newobj) /* was never loaded or saved before */
		savetype |= OBJ_SAVE_NEW;

	return savetype;
}

/**
 * @brief
 *	convert from DB to sched structure
 *
 * @param[out] ps - Address of the scheduler in pbs server
 * @param[in]  pdbsched  - Address of the database scheduler object
 *
 */
static int
db_to_sched(struct pbs_sched *ps, pbs_db_sched_info_t *pdbsched)
{
	strcpy(ps->sc_name, pdbsched->sched_name);

	if ((decode_attr_db(ps, &pdbsched->db_attr_list.attrs, sched_attr_idx, sched_attr_def, ps->sch_attr, SCHED_ATR_LAST, 0)) != 0)
		return -1;

	ps->newobj = 0;

	return 0;
}

/**
 * @brief
 *		Recover server information and attributes from server database
 *
 * @return	Error code
 * @retval	0	: On successful recovery and creation of server structure
 * @retval	-1	: On failure
 *
 */
int
svr_recov_db(void)
{
	void *conn = (void *) svr_db_conn;
	pbs_db_svr_info_t dbsvr = {0};
	pbs_db_obj_info_t obj;
	int rc = -1;

	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	rc = pbs_db_load_obj(conn, &obj);
	if (rc == -2)
		return 0; /* no change in server, return 0 */

	if (rc == 0)
		rc = db_to_svr(&server, &dbsvr);

	free_db_attr_list(&dbsvr.db_attr_list);

	return rc;
}

/**
 * @brief
 *		Save the state of the server, server quick save sub structure and
 *		optionally the attributes.
 *
 * @param[in]	ps   -	Pointer to struct server
 * @param[in]	mode -  type of save, either SVR_SAVE_QUICK or SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0	: Successful save of data.
 * @retval	-1	: Failure
 *
 */

int
svr_save_db(struct server *ps)
{
	void *conn = (void *) svr_db_conn;
	pbs_db_svr_info_t dbsvr = {0};
	pbs_db_obj_info_t obj;
	int savetype;
	int rc = -1;
	char *conn_db_err = NULL;

	/* as part of the server save, update svrlive file now,
	 * used in failover
	 */
	if (update_svrlive() != 0)
		goto done;

	if ((savetype = svr_to_db(ps, &dbsvr)) == -1)
		goto done;

	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	if ((rc = pbs_db_save_obj(conn, &obj, savetype)) == 0)
		ps->newobj = 0;

done:
	free_db_attr_list(&dbsvr.db_attr_list);

	if (rc != 0) {
		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to save server %s", conn_db_err ? conn_db_err : "");
		panic_stop_db();
		free(conn_db_err);
	}

	return (rc);
}

/**
 * @brief Recover Schedulers
 *
 * @param[in]	sname	- scheduler name
 * @param[in]	ps	- scheduler pointer, if any, to be updated
 *
 * @return	The recovered sched structure
 * @retval	NULL - Failure
 * @retval	!NULL - Success - address of recovered sched returned
 * */

pbs_sched *
sched_recov_db(char *sname, pbs_sched *ps)
{
	pbs_db_sched_info_t dbsched = {{0}};
	pbs_db_obj_info_t obj;
	void *conn = (void *) svr_db_conn;
	int rc = -1;
	char *conn_db_err = NULL;

	if (!ps) {
		if ((ps = sched_alloc(sname)) == NULL) {
			log_err(-1, __func__, "sched_alloc failed");
			return NULL;
		}
	}

	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;

	/* load sched */
	snprintf(dbsched.sched_name, sizeof(dbsched.sched_name), "%s", sname);

	rc = pbs_db_load_obj(conn, &obj);
	if (rc == -2)
		return ps; /* no change in sched */

	if (rc == 0)
		rc = db_to_sched(ps, &dbsched);
	else {
		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to load sched %s %s", sname, conn_db_err ? conn_db_err : "");
		free(conn_db_err);
	}

	free_db_attr_list(&dbsched.db_attr_list);

	if (rc != 0) {
		if (ps)
			sched_free(ps); /* free if we allocated here */
		ps = NULL;		/* so we return NULL */
	}
	return ps;
}

/**
 * @brief
 *		Save the state of the scheduler structure which consists only of attributes
 *
 * @param[in]	ps   -	Pointer to struct sched
 * @param[in]	mode -  type of save, only SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0 :	Successful save of data.
 * @retval	-1 :	Failure
 *
 */

int
sched_save_db(pbs_sched *ps)
{
	void *conn = (void *) svr_db_conn;
	pbs_db_sched_info_t dbsched = {{0}};
	pbs_db_obj_info_t obj;
	int savetype;
	int rc = -1;
	char *conn_db_err = NULL;

	if ((savetype = sched_to_db(ps, &dbsched)) == -1)
		goto done;

	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;

	if ((rc = pbs_db_save_obj(conn, &obj, savetype)) == 0)
		ps->newobj = 0;

done:
	free_db_attr_list(&dbsched.db_attr_list);

	if (rc != 0) {
		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to save sched %s %s", ps->sc_name, conn_db_err ? conn_db_err : "");
		panic_stop_db();
		free(conn_db_err);
	}

	return rc;
}

/**
 * @brief
* 	recov_sched_cb - callback function to process and load
* 			sched database result to pbs structure.
 *
 * @param[in]	dbobj	- database sched structure to C.
 * @param[out]	refreshed - if rows processed.
 *
 * @return	resv structure - on success
 * @return 	NULL - on failure
 */
pbs_sched *
recov_sched_cb(pbs_db_obj_info_t *dbobj, int *refreshed)
{
	pbs_sched *psched = NULL;
	pbs_db_sched_info_t *dbsched = dbobj->pbs_db_un.pbs_db_sched;

	*refreshed = 0;
	/* recover sched */
	if ((psched = sched_recov_db(dbsched->sched_name, NULL)) != NULL) {
		if (!strncmp(dbsched->sched_name, PBS_DFLT_SCHED_NAME, strlen(PBS_DFLT_SCHED_NAME)))
			dflt_scheduler = psched;
		psched->sc_conn_addr = get_hostaddr(get_sched_attr_str(psched, SCHED_ATR_SchedHost));
		set_scheduler_flag(SCH_CONFIGURE, psched);
		*refreshed = 1;
	}

	free_db_attr_list(&dbsched->db_attr_list);
	return psched;
}
