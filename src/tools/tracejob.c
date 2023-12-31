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
 * @file tracejob.c
 *
 * @brief
 *		tracejob.c - This file contains the functions related to the tracejob command.
 *
 * Functions included are:
 * 	get_cols()
 * 	main()
 * 	parse_log()
 * 	sort_by_date()
 * 	sort_by_message()
 * 	strip_path()
 * 	free_log_entry()
 * 	line_wrap()
 * 	log_path()
 * 	alloc_more_space()
 * 	filter_excess()
 *
 */
#include <pbs_config.h> /* the master config generated by configure */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <termios.h>
#if defined(HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif
#include "cmds.h"
#include "pbs_version.h"
#include "pbs_ifl.h"
#include "log.h"
#include "tracejob.h"

/* path from pbs home to the log files:
 index in mid_path must match with enum field in header */
const char *mid_path[] = {"server_priv/accounting", "server_logs", "mom_logs",
			  "sched_logs"};

struct log_entry *log_lines;
int ll_cur_amm;
int ll_max_amm;
int has_high_res_timestamp = 0;

static char none[1] = {'\0'};
/**
 * @brief
 * 		returns columns, in characters from winsize struct.
 *
 * @return	int
 * @retval	0	: failed.
 * @retval	columns, in characters	: success
 */
int
get_cols()
{

#ifdef WIN32
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		return (csbi.dwSize.X);
	}
	return (0);
#else
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
		return ws.ws_col;
	}
	return (0);
#endif
}
/**
 * @brief
 * 		This is main function of tracejob.
 *
 * @return	int
 * @retval	0	: success
 * @retval	1	: failure
 */
int
main(int argc, char *argv[])
{
	/* Array for the log entries for the specified job */
	FILE *fp;
	int i, j;
	char *filename; /* full path of logfile to read */
	struct tm *tm_ptr;
	int month, day, year;
	time_t t, t_save;
	signed char c;
	char *prefix_path = NULL;
	int number_of_days = 1;
	char *endp;
	short error = 0;
	int opt;
	char no_acct = 0, no_svr = 0, no_mom = 0, no_schd = 0;
	char verbose = 0;
	int wrap = -1;
	int log_filter = 0;
	int event_type;
	char filter_excessive = 0;
	int excessive_count;
#ifdef NAS /* localmod 022 */
	struct stat sbuf;
#endif /* localmod 022 */
	int unknw_job = 0;

	/*the real deal or output pbs_version and exit?*/
	PRINT_VERSION_AND_EXIT(argc, argv);

#if defined(FILTER_EXCESSIVE)
	filter_excessive = 1;
#endif

#if defined(EXCESSIVE_COUNT)
	excessive_count = EXCESSIVE_COUNT;
#endif

	pbs_loadconf(0);

	while ((c = getopt(argc, argv, "zvamslw:p:n:f:c:-:")) != EOF) {
		switch (c) {
			case 'v':
				verbose = 1;
				break;

			case 'a':
				no_acct = 1;
				break;

			case 's':
				no_svr = 1;
				break;

			case 'm':
				no_mom = 1;
				break;

			case 'l':
				no_schd = 1;
				break;

			case 'z':
				filter_excessive = filter_excessive ? 0 : 1;
				break;

			case 'c':
				excessive_count = strtol(optarg, &endp, 10);
				if (*endp != '\0')
					error = 1;
				break;

			case 'w':
				wrap = strtol(optarg, &endp, 10);
				if (*endp != '\0')
					error = 1;
				break;

			case 'p':
				prefix_path = optarg;
				break;

			case 'n':
				number_of_days = strtol(optarg, &endp, 10);
				if (*endp != '\0')
					error = 1;
				break;

			case 'f':
				if (strcmp(optarg, "error") == 0)
					log_filter |= PBSEVENT_ERROR;
				else if (strcmp(optarg, "system") == 0)
					log_filter |= PBSEVENT_SYSTEM;
				else if (strcmp(optarg, "admin") == 0)
					log_filter |= PBSEVENT_ADMIN;
				else if (strcmp(optarg, "job") == 0)
					log_filter |= PBSEVENT_JOB;
				else if (strcmp(optarg, "job_usage") == 0)
					log_filter |= PBSEVENT_JOB_USAGE;
				else if (strcmp(optarg, "security") == 0)
					log_filter |= PBSEVENT_SECURITY;
				else if (strcmp(optarg, "sched") == 0)
					log_filter |= PBSEVENT_SCHED;
				else if (strcmp(optarg, "debug") == 0)
					log_filter |= PBSEVENT_DEBUG;
				else if (strcmp(optarg, "debug2") == 0)
					log_filter |= PBSEVENT_DEBUG2;
				else if (strcmp(optarg, "resv") == 0)
					log_filter |= PBSEVENT_RESV;
				else if (strcmp(optarg, "debug3") == 0)
					log_filter |= PBSEVENT_DEBUG3;
				else if (strcmp(optarg, "debug4") == 0)
					log_filter |= PBSEVENT_DEBUG4;
				else if (strcmp(optarg, "force") == 0)
					log_filter |= PBSEVENT_FORCE;
				else if (isdigit(optarg[0])) {
					log_filter = strtol(optarg, &endp, 16);
					if (*endp != '\0')
						error = 1;
				} else
					error = 1;
				break;

			default:
				error = 1;
		}
	}

	/* no jobs */
	if (error || argc == optind) {
		printf(
			"USAGE: %s [-a|s|l|m|v] [-w size] [-p path] [-n days] [-f filter_type] job_identifier...\n",
			strip_path(argv[0]));

		printf(
			"   -p : path to PBS_HOME\n"
			"   -w : number of columns of your terminal\n"
			"   -n : number of days in the past to look for job(s) [default 1]\n"
			"   -f : filter out types of log entries, multiple -f's can be specified\n"
			"        error, system, admin, job, job_usage, security, sched, debug, \n"
			"        debug2, resv, debug3, debug4, force or absolute numberic equiv\n"
			"   -z : toggle filtering excessive messages\n"
			"   -c : what message count is considered excessive\n"
			"   -a : don't use accounting log files\n"
			"   -s : don't use server log files\n"
			"   -l : don't use scheduler log files\n"
			"   -m : don't use mom log files\n"
			"   -v : verbose mode - show more error messages\n");

		printf("\n       %s --version\n", strip_path(argv[0]));
		printf("   --version : display version only\n\n");

		printf("default prefix path = %s\n", pbs_conf.pbs_home_path);
#if defined(FILTER_EXCESSIVE)
		printf("filter_excessive: ON\n");
#else
		printf("filter_excessive: OFF\n");
#endif
		return 1;
	}

	if (wrap == -1)
		wrap = get_cols();

	time(&t);
	t_save = t;

	for (opt = optind; opt < argc; opt++) {
		ll_cur_amm = 0; /* reset line count to zero */
		for (i = 0, t = t_save; i < number_of_days; i++, t -= SECONDS_IN_DAY) {
			tm_ptr = localtime(&t);
			month = tm_ptr->tm_mon;
			day = tm_ptr->tm_mday;
			year = tm_ptr->tm_year;

			for (j = 0; j < 4; j++) {
				if ((j == IND_ACCT && no_acct) || (j == IND_SERVER && no_svr) ||
				    (j == IND_MOM && no_mom) || (j == IND_SCHED && no_schd))
					continue;

#ifdef NAS /* localmod 022 */
				filename = log_path(prefix_path, j, 1, month, day, year);
				if (stat(filename, &sbuf) == -1) {
					filename = log_path(prefix_path, j, 0, month, day, year);
				}
#else
				filename = log_path(prefix_path, j, month, day, year);
#endif /* localmod 022 */

				if ((fp = fopen(filename, "r")) == NULL) {
					if (verbose)
						perror(filename);
					continue;
				}

				parse_log(fp, argv[opt], j);

				fclose(fp);
			}
		}

		if (filter_excessive)
			filter_excess(excessive_count);

		qsort(log_lines, ll_cur_amm, sizeof(struct log_entry), sort_by_date);

		if (ll_cur_amm != 0) {
			printf("\nJob: %s\n\n", log_lines[0].name);
			for (i = 0; i < ll_cur_amm; i++) {
				if (log_lines[i].log_file == 'A')
					event_type = 0;
				else
					event_type = strtol(log_lines[i].event, &endp, 16);
				if (!(log_filter & event_type) && !(log_lines[i].no_print)) {
					if (has_high_res_timestamp) {
						printf("%-27s %-5c", log_lines[i].date, log_lines[i].log_file);
						line_wrap(log_lines[i].msg, 33, wrap);
					} else {
						printf("%-20s %-5c", log_lines[i].date, log_lines[i].log_file);
						line_wrap(log_lines[i].msg, 26, wrap);
					}
				}
			}
		} else {
			/*
			 * if line count is zero, it means that there is
			 * no job associated with the job-id given.
			 */
			unknw_job = 1;
			if (strchr(argv[opt], '.') == NULL)
				fprintf(stderr, "\ntracejob: Couldn't find Job Id %s.%s in logs of past %d day%s\n\n", argv[opt],
					pbs_conf.pbs_server_name, number_of_days, number_of_days == 1 ? "" : "s");
			else
				fprintf(stderr, "\ntracejob: Couldn't find Job Id %s in logs of past %d day%s\n\n", argv[opt],
					number_of_days, number_of_days == 1 ? "" : "s");
		}
	}

	/* return 1 if unknown job-id */
	if (unknw_job)
		return 1;

	return 0;
}

/**
 * @brief
 *		parse_log - parse out entires of a log file for a specific job
 *		    and return them in log_entry structures
 *
 * @param[in]	fp	-	the log file
 * @param[in]	job	-	the name of the job
 * @param[in]	ind	-	which log file - index in enum index
 *
 *	@return	nothing
 *	@note
 *		modifies global variables: loglines, ll_cur_amm, ll_max_amm
 *
 * @par MT-safe: No
 */
void
parse_log(FILE *fp, char *job, int ind)
{
	struct log_entry tmp; /* temporary log entry */
	char *buf;	      /* buffer to read in from file */
	char *tbuf;	      /* temporarily hold realloc's for main buffer */
	char job_buf[128];    /* hold the jobid and the . */
	char *p;	      /* pointer to use for strtok */
	int field_count;      /* which field in log entry */
	int j = 0;
	struct tm tms; /* used to convert date to unix date */
	int lineno = 0;
	int slen;
	char *pdot;
	int buf_size = 16384; /* initial buffer size */
	int break_fl = 0;

	buf = (char *) calloc(buf_size, sizeof(char));
	if (!buf)
		return;

	tms.tm_isdst = -1; /* mktime() will attempt to figure it out */

	strcpy(job_buf, job);

	while (fgets(buf, buf_size, fp) != NULL) {
		while (buf_size == (strlen(buf) + 1)) {
			buf_size *= 2;
			tbuf = (char *) realloc(buf, (buf_size + 1) * sizeof(char));
			if (!tbuf) {
				break_fl = 1;
				break;
			}
			buf = tbuf;
			if (fgets(buf + strlen(buf), buf_size / 2 + 1, fp) == NULL) {
				break_fl = 1;
				break;
			}
		}
		if (break_fl)
			break;
		lineno++;
		j++;
		buf[strlen(buf) - 1] = '\0';
		p = strtok(buf, ";");
		field_count = 0;
		memset(&tmp, 0, sizeof(struct log_entry));

		for (field_count = 0; field_count < 6 && p != NULL; field_count++) {
			switch (field_count) {
				case FLD_DATE:
					tmp.date = p;
					if (ind == IND_ACCT)
						field_count = 2;
					break;

				case FLD_EVENT:
					tmp.event = p;
					break;

				case FLD_OBJ:
					tmp.obj = p;
					break;

				case FLD_TYPE:
					tmp.type = p;
					break;

				case FLD_NAME:
					tmp.name = p;
					break;

				case FLD_MSG:
					tmp.msg = p;
					break;

				default:
					printf("Field count too big!\n");
					printf("%s\n", p);
			}

			p = strtok(NULL, ";");
		}

		pdot = strchr(job_buf, (int) '.');
		if (pdot == NULL && tmp.name != NULL) {
			int tlen = strlen(job_buf);

			slen = strcspn(tmp.name, ".");
			if (tlen > slen)
				slen = tlen;
		} else
			slen = strlen(job_buf);

		if (tmp.name != NULL && strncmp(job_buf, tmp.name, slen) == 0) {
			if (ll_cur_amm >= ll_max_amm)
				alloc_more_space();

			free_log_entry(&log_lines[ll_cur_amm]);

			if (tmp.date != NULL) {
				/*
				 * We need to parse the time string.
				 * The string will either have high res logging or not.
				 * The high res logging is after the dot after the seconds field.
				 */
				log_lines[ll_cur_amm].date = strdup(tmp.date);
				if ((ind != IND_ACCT) && (strchr(tmp.date, '.'))) {
					/* Parse time string looking for high res logging.  If we don't parse 7 fields, we have a invalid log time. */
					if (sscanf(tmp.date, "%d/%d/%d %d:%d:%d.%ld", &tms.tm_mon,
						   &tms.tm_mday, &tms.tm_year, &tms.tm_hour, &tms.tm_min,
						   &tms.tm_sec, &(log_lines[ll_cur_amm].highres)) != 7) {
						log_lines[ll_cur_amm].date_time = -1; /* error in date field */
						log_lines[ll_cur_amm].highres = NO_HIGH_RES_TIMESTAMP;
					} else { /* We found all 7 fields, correctly formed time string */
						has_high_res_timestamp = 1;
						if (tms.tm_year > 1900)
							tms.tm_year -= 1900;
						/* The number of months since January,
 						 * in the range 0 to 11 for mktime()
 						 */
						tms.tm_mon--;
						log_lines[ll_cur_amm].date_time = mktime(&tms);
					}
				} else { /* Normal time string */
					if (sscanf(tmp.date, "%d/%d/%d %d:%d:%d", &tms.tm_mon, &tms.tm_mday,
						   &tms.tm_year, &tms.tm_hour, &tms.tm_min, &tms.tm_sec) != 6) {
						log_lines[ll_cur_amm].date_time = -1; /* error in date field */
					} else {				      /* We found all 6 fields, correctly formed time string */
						if (tms.tm_year > 1900)
							tms.tm_year -= 1900;
						tms.tm_mon--; /* The number of months since January, in the range 0 to 11 for mktime */
						log_lines[ll_cur_amm].date_time = mktime(&tms);
					}
					log_lines[ll_cur_amm].highres = NO_HIGH_RES_TIMESTAMP;
				}
			}
			if (tmp.event != NULL)
				log_lines[ll_cur_amm].event = strdup(tmp.event);
			else
				log_lines[ll_cur_amm].event = none;
			if (tmp.obj != NULL)
				log_lines[ll_cur_amm].obj = strdup(tmp.obj);
			else
				log_lines[ll_cur_amm].obj = none;
			if (tmp.type != NULL)
				log_lines[ll_cur_amm].type = strdup(tmp.type);
			else
				log_lines[ll_cur_amm].type = none;
			if (tmp.name != NULL)
				log_lines[ll_cur_amm].name = strdup(tmp.name);
			else
				log_lines[ll_cur_amm].name = none;
			if (tmp.msg != NULL)
				log_lines[ll_cur_amm].msg = strdup(tmp.msg);
			else
				log_lines[ll_cur_amm].msg = none;
			switch (ind) {
				case IND_SERVER:
					log_lines[ll_cur_amm].log_file = 'S';
					break;

				case IND_SCHED:
					log_lines[ll_cur_amm].log_file = 'L';
					break;

				case IND_ACCT:
					log_lines[ll_cur_amm].log_file = 'A';
					break;

				case IND_MOM:
					log_lines[ll_cur_amm].log_file = 'M';
					break;
				default:
					log_lines[ll_cur_amm].log_file = 'U'; /* undefined */
			}
			log_lines[ll_cur_amm].lineno = lineno;
			ll_cur_amm++;
		}
	}
	free(buf);
}

/**
 * @brief
 *		sort_by_date - compare function for qsort.  It compares two time_t
 *			variables and high resolution time stamp (if set)
 *
 * @param[in]	v1	-	log_entry structure1 which contains time_t variables.
 * @param[in]	v1	-	log_entry structure2 which contains time_t variables.
 *
 * @return	int
 * @retval	0	: both are same.
 * @retval	-1	: v1 is lesser than v2
 * @retval	1	: v1 is greater than v2
 */

int
sort_by_date(const void *v1, const void *v2)
{
	struct log_entry *l1, *l2;

	l1 = (struct log_entry *) v1;
	l2 = (struct log_entry *) v2;

	if (l1->date_time < l2->date_time)
		return -1;
	else if (l1->date_time > l2->date_time)
		return 1;
	else {
		if ((l1->highres != NO_HIGH_RES_TIMESTAMP) && (l2->highres != NO_HIGH_RES_TIMESTAMP)) {
			if (l1->highres < l2->highres)
				return -1;
			else if (l1->highres > l2->highres)
				return 1;
		}

		if (l1->log_file == l2->log_file) {
			if (l1->lineno < l2->lineno)
				return -1;
			else if (l1->lineno > l2->lineno)
				return 1;
		}
		return 0;
	}
}

/**
 * @brief
 *		sort_by_message - compare function used by qsort.  Compares the message
 *
 * @param[in]	v1	-	log_entry structure1 which contains message to be compared.
 * @param[in]	v1	-	log_entry structure2 which contains message to be compared.
 *
 * @return	return value from strcmp by passing v1 and v2 as arguments.
 */
int
sort_by_message(const void *v1, const void *v2)
{
	return strcmp(((struct log_entry *) v1)->msg, ((struct log_entry *) v2)->msg);
}

/**
 * @brief
 *		strip_path - strips all leading path and returns the command name
 *			i.e. /usr/bin/vi => vi
 *
 * @param[in]	path	-	the path to strip
 *
 * @return	striped path
 *
 */
char *
strip_path(char *path)
{
	char *p;

	p = path + strlen(path);

	while (p != path && *p != '/')
		p--;

	if (*p == '/')
		p++;

	return p;
}

/**
 * @brief
 *		free_log_entry - free the interal data used by a log entry
 *
 * @param[in,out]	lg	-	log entry to free
 *
 * @return nothing
 *
 */
void
free_log_entry(struct log_entry *lg)
{
	if (lg->date != NULL && lg->date != none)
		free(lg->date);

	lg->date = NULL;

	if (lg->event != NULL && lg->event != none)
		free(lg->event);

	lg->event = NULL;

	if (lg->obj != NULL && lg->obj != none)
		free(lg->obj);

	lg->obj = NULL;

	if (lg->type != NULL && lg->type != none)
		free(lg->type);

	lg->type = NULL;

	if (lg->name != NULL && lg->name != none)
		free(lg->name);

	lg->name = NULL;

	if (lg->msg != NULL && lg->msg != none)
		free(lg->msg);

	lg->msg = NULL;

	lg->log_file = '\0';
}

/**
 * @brief
 *		line_wrap - wrap lines at word margin and print
 *		    The first line will be printed.  The rest will be indented
 *		    by start and will not go over a max line length of end chars
 *
 *
 * @param[in]	line	-	the line to wrap
 * @param[in]	start	-	amount of whitespace to indent subsequent lines
 * @param[in]	end	-	number of columns in the terminal
 *
 * @return	nothing
 *
 */
void
line_wrap(char *line, int start, int end)
{
	int wrap_at;
	int total_size;
	int start_index;
	char *cur_ptr;
	char *start_ptr;

	start_ptr = line;
	total_size = strlen(line);
	wrap_at = (end > start) ? (end - start) : 0;
	start_index = 0;

	if (end == 0)
		printf("%s\n", show_nonprint_chars(line));
	else {
		while (start_index < total_size) {
			if (start_index + wrap_at < total_size) {
				cur_ptr = start_ptr + wrap_at;

				while (cur_ptr > start_ptr && *cur_ptr != ' ')
					cur_ptr--;

				if (cur_ptr == start_ptr) {
					cur_ptr = start_ptr + wrap_at;
					while (*cur_ptr != ' ' && *cur_ptr != '\0')
						cur_ptr++;
				}

				*cur_ptr = '\0';
			} else
				cur_ptr = line + total_size;

			/* first line, don't indent */
			if (start_ptr == line)
				printf("%s\n", show_nonprint_chars(start_ptr));
			else
				printf("%*s%s\n", start, " ", show_nonprint_chars(start_ptr));

			start_ptr = cur_ptr + 1;
			start_index = cur_ptr - line;
		}
	}
}

/**
 * brief
 *		log_path - create the path to a log file
 *
 * @param[out]	path	-	prefix path
 * @param[in]	index	-	index into the prefix_path array
 * @param[in]	old	-	old date or new
 * @param[in]	month	-	month in numeric starts from 0.
 * @param[in]	day	-	day in numeric.
 * @param[in]	year	-	year as a count from 1900.
 *
 * @return path to log file
 *
 * @par MT-safe:	No
 */
#ifdef NAS /* localmod 022 */
char *
log_path(char *path, int index, int old, int month, int day, int year)
{
	static char buf[256];
	char *oldd;

	oldd = old ? "/old.d" : "";
	if (pbs_conf.pbs_mom_home && index == IND_MOM)
		path = pbs_conf.pbs_mom_home;

	if (path != NULL)
		sprintf(buf, "%s/%s%s/%04d%02d%02d", path, mid_path[index], oldd,
			year + 1900, month + 1, day);
	else
		sprintf(buf, "%s/%s%s/%04d%02d%02d", pbs_conf.pbs_home_path,
			mid_path[index], oldd,
			year + 1900, month + 1, day);

	return buf;
}
#else
/**
 * brief
 *		log_path - create the path to a log file
 *
 * @param[out]	path	-	prefix path
 * @param[in]	index	-	index into the prefix_path array
 * @param[in]	month	-	month in numeric starts from 0.
 * @param[in]	day	-	day in numeric.
 * @param[in]	year	-	year as a count from 1900.
 *
 * @return path to log file
 *
 * @par MT-safe:	No
 */
char *
log_path(char *path, int index, int month, int day, int year)
{
	static char buf[256];

	if (pbs_conf.pbs_mom_home && index == IND_MOM)
		path = pbs_conf.pbs_mom_home;

	if (path != NULL)
		sprintf(buf, "%s/%s/%04d%02d%02d", path, mid_path[index],
			year + 1900, month + 1, day);
	else
		sprintf(buf, "%s/%s/%04d%02d%02d", pbs_conf.pbs_home_path, mid_path[index],
			year + 1900, month + 1, day);

	return buf;
}
#endif /* localmod 022 */

/**
 * @brief
 *		alloc_space - double the allocation of current log entires
 *
 */
void
alloc_more_space()
{
	int old_amm = ll_max_amm;
	struct log_entry *temp_log_lines;

	if (ll_max_amm == 0)
		ll_max_amm = DEFAULT_LOG_LINES;
	else
		ll_max_amm *= 2;

	if ((temp_log_lines = realloc(log_lines, ll_max_amm * sizeof(struct log_entry))) == NULL) {
		perror("Error allocating memory");
		exit(1);
	} else
		log_lines = temp_log_lines;

	memset(&log_lines[old_amm], 0, (ll_max_amm - old_amm) * sizeof(struct log_entry));
}

/**
 * @brief
 *		filter_excess - count and set the no_print flags if the count goes over
 *		       the message thrashold
 *
 * @param[in]	threshold	-	if the number of messages exceeds this, don't print them
 *
 * @return	nothing
 *
 * @note
 *		log_lines array will be sorted in place
 */
void
filter_excess(int threshold)
{
	int cur_count = 1;
	char *msg;
	int i;
	int j = 0;

	if (ll_cur_amm) {
		qsort(log_lines, ll_cur_amm, sizeof(struct log_entry), sort_by_message);
		msg = log_lines[0].msg;

		for (i = 1; i < ll_cur_amm; i++) {
			if (strcmp(log_lines[i].msg, msg) == 0)
				cur_count++;
			else {
				if (cur_count >= threshold) {
					/* we want to print 1 of the many messages */
					for (; j < i - 1; j++)
						log_lines[j].no_print = 1;
				}

				j = i;
				cur_count = 1;
				msg = log_lines[i].msg;
			}
		}

		if (cur_count >= threshold) {
			j++;
			for (; j < i; j++)
				log_lines[j].no_print = 1;
		}
	}
}
