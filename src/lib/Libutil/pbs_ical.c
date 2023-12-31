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
 * @file	pbs_ical.c
 * @brief
 * pbs_ical.c : Utility functions to parse and handle iCal syntax
 *
 *	This is used to abstract the use of libical from the
 *  functionality of PBS. It is invoked from
 *  1) the commands (pbs_rsub and pbs_rstat)
 *  2) the server: req_rescq.c, svr_jobfunc.c
 *  3) the scheduler: resv_info.c
 *
 *  The purpose of this interface to libical is to wrap all iCalendar specific
 *  calls from PBS to an implementation independent function.
 *
 * See RFC 2445 for more information and specifics of the iCalendar syntax
 *
 */

#include <pbs_config.h> /* the master config generated by configure */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libutil.h>

#include "pbs_error.h"
#ifdef LIBICAL
#include <libical/ical.h>
#endif

#define DATE_LIMIT (3 * (60 * 60 * 24 * 365)) /* Limit to 3 years from now */

/**
 * @brief
 * 	Returns the number of occurrences defined by a recurrence rule.
 *
 * @par	The total number of occurrences is currently limited to a hardcoded
 * 	3 years limit from the current date.
 *
 * @par	NOTE: Determine whether 3 years limit is the right way to go about setting
 * 	a limit on the total number of occurrences.
 *
 * @param[in] rrule - The recurrence rule as defined by the user
 * @param[in] tt - The start time of the first occurrence
 * @param[in] tz - The timezone associated to the recurrence rule
 *
 * @return	int
 * @retval 	the total number of occurrences
 *
 */
int
get_num_occurrences(char *rrule, time_t dtstart, char *tz)
{

#ifdef LIBICAL
	struct icalrecurrencetype rt;
	struct icaltimetype start;
	icaltimezone *localzone;
	struct icaltimetype next;
	struct icalrecur_iterator_impl *itr;
	time_t now;
	time_t date_limit;
	int num_resv = 0;

	/* if any of the argument is NULL, we are dealing with
	 * advance reservation, so return 1 occurrence */
	if (rrule == NULL || tz == NULL)
		return 1;

	icalerror_clear_errno();

	icalerror_set_error_state(ICAL_PARSE_ERROR, ICAL_ERROR_NONFATAL);
#ifdef LIBICAL_API2
	icalerror_set_errors_are_fatal(0);
#else
	icalerror_errors_are_fatal = 0;
#endif
	localzone = icaltimezone_get_builtin_timezone(tz);

	if (localzone == NULL)
		return 0;

	now = time(NULL);
	date_limit = now + DATE_LIMIT;

	rt = icalrecurrencetype_from_string(rrule);

	start = icaltime_from_timet_with_zone(dtstart, 0, NULL);
	icaltimezone_convert_time(&start, icaltimezone_get_utc_timezone(), localzone);

	itr = (struct icalrecur_iterator_impl *) icalrecur_iterator_new(rt, start);

	next = icalrecur_iterator_next(itr);

	/* Compute the total number of occurrences.
	 * Breaks out if the total number of allowed occurrences is exceeded */
	while (!icaltime_is_null_time(next) &&
	       (icaltime_as_timet(next) < date_limit)) {
		num_resv++;
		next = icalrecur_iterator_next(itr);
	}
	icalrecur_iterator_free(itr);

	return num_resv;
#else

	if (rrule == NULL)
		return 1;

	return 0;
#endif
}

/**
 * @brief
 * 	Get the occurrence as defined by the given recurrence rule,
 * 	index, and start time. This function assumes that the
 * 	time dtsart passed in is the one to start the occurrence from.
 *
 * @par	NOTE: This function should be made reentrant such that
 * 	it can be looped over without having to loop over every occurrence
 * 	over and over again.
 *
 * @param[in] rrule - The recurrence rule as defined by the user
 * @param[in] dtstart - The start time from which to start
 * @param[in] tz - The timezone associated to the recurrence rule
 * @param[in] idx - The index of the occurrence to start counting from
 *
 * @return 	time_t
 * @retval	The date of the next occurrence or -1 if the date exceeds libical's
 * 		Unix time in 2038
 *
 */
time_t
get_occurrence(char *rrule, time_t dtstart, char *tz, int idx)
{
#ifdef LIBICAL
	struct icalrecurrencetype rt;
	struct icaltimetype start;
	icaltimezone *localzone;
	struct icaltimetype next;
	struct icalrecur_iterator_impl *itr;
	int i;
	time_t next_occr = dtstart;

	if (rrule == NULL)
		return dtstart;

	if (tz == NULL)
		return -1;

	icalerror_clear_errno();

	icalerror_set_error_state(ICAL_PARSE_ERROR, ICAL_ERROR_NONFATAL);
#ifdef LIBICAL_API2
	icalerror_set_errors_are_fatal(0);
#else
	icalerror_errors_are_fatal = 0;
#endif
	localzone = icaltimezone_get_builtin_timezone(tz);

	if (localzone == NULL)
		return -1;

	rt = icalrecurrencetype_from_string(rrule);

	start = icaltime_from_timet_with_zone(dtstart, 0, NULL);
	icaltimezone_convert_time(&start, icaltimezone_get_utc_timezone(), localzone);
	next = start;

	itr = (struct icalrecur_iterator_impl *) icalrecur_iterator_new(rt, start);
	/* Skip as many occurrences as specified by idx */
	for (i = 0; i < idx && !icaltime_is_null_time(next); i++)
		next = icalrecur_iterator_next(itr);

	if (!icaltime_is_null_time(next)) {
		icaltimezone_convert_time(&next, localzone,
					  icaltimezone_get_utc_timezone());
		next_occr = icaltime_as_timet(next);
	} else
		next_occr = -1; /* If reached end of possible date-time return -1 */
	icalrecur_iterator_free(itr);

	return next_occr;
#else
	return dtstart;
#endif
}

/**
 * @brief
 * 	Check if a recurrence rule is valid and consistent.
 * 	The recurrence rule is verified against a start date and checks
 * 	that the frequency of the recurrence matches the duration of the
 * 	submitted reservation. If the duration of a reservation exceeds the
 * 	granularity of the frequency then an error message is displayed.
 *
 * @par The recurrence rule is checked to contain a COUNT or an UNTIL.
 *
 * @par	Note that the PBS_TZID environment variable HAS to be set for the occurrence's
 * 	dates to be correctly computed.
 *
 * @param[in] rrule - The recurrence rule to unroll
 * @param[in] dtstart - The start time associated to the reservation (1st occurrence)
 * @param[in] dtend - The end time associated to the reservation (1st occurrence)
 * @param[in] duration - The duration of an occurrence. This is used when a reservation is
 *  			submitted using the -D (duration) param instead of an end time
 * @param[in] tz - The timezone associated to the recurrence rule
 * @param[in] err_code - A pointer to the error code to return. Codes are defined in pbs_error.h
 *
 * @return	int
 * @retval	The total number of occurrences that the recurrence rule and start date
 *
 * 		define. 1 for an advance reservation.
 *
 */
int
check_rrule(char *rrule, time_t dtstart, time_t dtend, char *tz, int *err_code)
{

#ifdef LIBICAL /* Standing Reservation Recurrence */
	int count = 1;
	struct icalrecurrencetype rt;
	struct icaltimetype start;
	struct icaltimetype next;
	struct icaltimetype first;
	struct icaltimetype prev;
	struct icalrecur_iterator_impl *itr;
	icaltimezone *localzone;
	int time_err = 0;
	int i;
	long min_occr_duration = -1;
	long tmp_occr_duration = 0;
	long duration;

	*err_code = 0;
	icalerror_clear_errno();

	icalerror_set_error_state(ICAL_PARSE_ERROR, ICAL_ERROR_NONFATAL);
#ifdef LIBICAL_API2
	icalerror_set_errors_are_fatal(0);
#else
	icalerror_errors_are_fatal = 0;
#endif

	if (tz == NULL || rrule == NULL)
		return 0;

	localzone = icaltimezone_get_builtin_timezone(tz);
	/* If the timezone info directory is not accessible
	 * then bail
	 */
	if (localzone == NULL) {
		*err_code = PBSE_BAD_ICAL_TZ;
		return 0;
	}

	rt = icalrecurrencetype_from_string(rrule);

	/* Check if by_day rules are defined and valid
	 * the first item in the array of by_* rule
	 * determines whether the item exists or not.
	 */
	for (i = 0; rt.by_day[i] < 8; i++) {
		if (rt.by_day[i] <= 0) {
			*err_code = PBSE_BAD_RRULE_SYNTAX;
			return 0;
		}
	}

	/* Check if by_hour rules are defined and valid
	 * the first item in the array of by_* rule
	 * determines whether the item exists or not.
	 */
	for (i = 0; rt.by_hour[i] < 25; i++) {
		if (rt.by_hour[i] < 0) {
			*err_code = PBSE_BAD_RRULE_SYNTAX;
			return 0;
		}
	}

	/* Check if frequency was correctly set */
	if (rt.freq == ICAL_NO_RECURRENCE) {
		*err_code = PBSE_BAD_RRULE_SYNTAX;
		return 0;
	}
	/* Check if the rest of the by_* rules are defined
	 * and valid.
	 * currently no support for
	 * BYMONTHDAY, BYYEARDAY, BYSECOND,
	 * BYMINUTE, BYWEEKNO, or BYSETPOS
	 *
	 */
	if (rt.by_second[0] < 61 ||    /* by_second is negative such as in -10 */
	    rt.by_minute[0] < 61 ||    /* by_minute is negative such as in -10 */
	    rt.by_year_day[0] < 367 || /* a year day is defined */
	    rt.by_month_day[0] < 31 || /* a month day is defined */
	    rt.by_week_no[0] < 52 ||   /* a week number is defined */
	    rt.by_set_pos[0] < 367) {  /* a set pos is defined */
		*err_code = PBSE_BAD_RRULE_SYNTAX;
		return 0;
	}

	/* Require that either a COUNT or UNTIL be passed. But not both. */
	if ((rt.count == 0 && icaltime_is_null_time(rt.until)) ||
	    (rt.count != 0 && !icaltime_is_null_time(rt.until))) {
		*err_code = PBSE_BAD_RRULE_SYNTAX2; /* Undefined iCalendar synax. A valid COUNT or UNTIL is required */
		return 0;
	}

	start = icaltime_from_timet_with_zone(dtstart, 0, NULL);
	icaltimezone_convert_time(&start, icaltimezone_get_utc_timezone(), localzone);

	itr = (struct icalrecur_iterator_impl *) icalrecur_iterator_new(rt, start);

	duration = dtend - dtstart;

	/* First check if the syntax of the iCalendar rule is valid */
	next = icalrecur_iterator_next(itr);

	/* Catch case where first occurrence date is in the past */
	if (icaltime_is_null_time(next)) {
		*err_code = PBSE_BADTSPEC;
		icalrecur_iterator_free(itr);
		return 0;
	}

	first = next;
	prev = first;

	for (next = icalrecur_iterator_next(itr); !icaltime_is_null_time(next); next = icalrecur_iterator_next(itr), count++) {
		/* The interval duration between two occurrences
		 * is the time between the end of an occurrence and the
		 * start of the next one
		 */
		tmp_occr_duration = icaltime_as_timet(next) - icaltime_as_timet(prev);

		/* Set the minimum time interval between occurrences */
		if (min_occr_duration == -1)
			min_occr_duration = tmp_occr_duration;
		else if (tmp_occr_duration > 0 &&
			 tmp_occr_duration < min_occr_duration)
			min_occr_duration = tmp_occr_duration;

		prev = next;
	}
	/* clean up */
	icalrecur_iterator_free(itr);

	if (icalerrno != ICAL_NO_ERROR) {
		*err_code = PBSE_BAD_RRULE_SYNTAX; /*  Undefined iCalendar syntax */
		return 0;
	}

	/* Then check if the duration fits in the frequency rule */
	switch (rt.freq) {

		case ICAL_SECONDLY_RECURRENCE: {
			if (duration > 1) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								     /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_SECONDLY; /* SECONDLY recurrence duration cannot exceed 1 second */
				time_err++;
			}
			break;
		}
		case ICAL_MINUTELY_RECURRENCE: {
			if (duration > 60) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								     /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_MINUTELY; /* MINUTELY recurrence duration cannot exceed 1 minute */
				time_err++;
			}
			break;
		}
		case ICAL_HOURLY_RECURRENCE: {
			if (duration > (60 * 60)) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								   /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_HOURLY; /* HOURLY recurrence duration cannot exceed 1 hour */
				time_err++;
			}
			break;
		}
		case ICAL_DAILY_RECURRENCE: {
			if (duration > (60 * 60 * 24)) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								  /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_DAILY; /* DAILY recurrence duration cannot exceed 24 hours */
				time_err++;
			}
			break;
		}
		case ICAL_WEEKLY_RECURRENCE: {
			if (duration > (60 * 60 * 24 * 7)) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								   /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_WEEKLY; /* WEEKLY recurrence duration cannot exceed 1 week */
				time_err++;
			}
			break;
		}
		case ICAL_MONTHLY_RECURRENCE: {
			if (duration > (60 * 60 * 24 * 30)) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								    /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_MONTHLY; /* MONTHLY recurrence duration cannot exceed 1 month */
				time_err++;
			}
			break;
		}
		case ICAL_YEARLY_RECURRENCE: {
			if (duration > (60 * 60 * 24 * 30 * 365)) {
#ifdef NAS /* localmod 005 */
				icalerrno = ICAL_BADARG_ERROR;
#else
				icalerrno = 1;
#endif								   /* localmod 005 */
				*err_code = PBSE_BAD_RRULE_YEARLY; /* YEARLY recurrence duration cannot exceed 1 year */
				time_err++;
			}
			break;
		}
		default: {
			icalerror_set_errno(ICAL_MALFORMEDDATA_ERROR);
			return 0;
		}
	}

	if (time_err)
		return 0;

	/* If the requested reservation duration exceeds
	 * the occurrence's duration then print an error
	 * message and return */
	if (count != 1 && duration > min_occr_duration) {
		*err_code = PBSE_BADTSPEC; /*  Bad Time Specification(s) */
		return 0;
	}

	return count;

#else

	*err_code = PBSE_BAD_RRULE_SYNTAX; /* iCalendar is undefined */
	return 0;
#endif
}

/**
 * @brief
 * 	Displays the occurrences in a two-column format:
 * 	the first column corresponds to the occurrence date and time
 * 	the second column corresponds to the reserved execvnode
 *
 * @par	NOTE: This is currently only used by pbs_rstat and only
 * 	for testing purpose, it is not used in production environment.
 *
 * @param[in] rrule - The recurrence rule considered
 * @param[in] dtstart - The date start of the recurrence
 * @param[in] seq_execvnodes - The condensed form of execvnodes/occurrence
 * @param[in] tz - The timezone associated to the recurrence rule
 * @param[in] ridx - The index of the occurrence from which to start displaying information
 * @param[in] count - The total number of occurrences considered
 *
 */
void
display_occurrences(char *rrule, time_t dtstart, char *seq_execvnodes, char *tz, int ridx, int count)
{

#ifdef LIBICAL
	char **short_xc;
	char **tofree;
	char *tt_str;
	time_t next = 0;
	int i = 1;

	if (tz == NULL || rrule == NULL)
		return;

	if (get_num_occurrences(rrule, dtstart, tz) == 0)
		return;

	short_xc = (char **) unroll_execvnode_seq(seq_execvnodes, &tofree);

	printf("Occurrence Dates\t Occurrence Execvnode:\n");
	for (; ridx <= count && next != -1; ridx++) {
		next = get_occurrence(rrule, dtstart, tz, i);
		i++;
		tt_str = ctime(&next);
		/* ctime adds a carriage return at the end, strip it */
		tt_str[strlen(tt_str) - 1] = '\0';
		printf("%2s %2s\n", tt_str, short_xc[ridx - 1]);
	}
	free_execvnode_seq(tofree);
	free(short_xc);
#endif
}

/**
 * @brief
 * 	Set the zoneinfo directory
 *
 * @param[in] path - The path to libical's zoneinfo
 *
 */
void
set_ical_zoneinfo(char *path)
{
#ifdef LIBICAL
	static int called = 0;
	if (path != NULL) {
		if (called)
			free_zone_directory();

		set_zone_directory(path);
		called = 1;
	}
#endif
	return;
}

#ifdef DEBUG
/**
 * @brief
 *	display the number of occurences and occurences.
 *
 * @param[in] rrule - The recurrence rule considered
 * @param[in] dtstart - The date start of the recurrence
 * @param[in] tz - The timezone associated to the recurrence rule
 *
 */
void
test_it(char *rrule, time_t dtstart, char *tz)
{
	int no;
	time_t next;
	int i;

	no = get_num_occurrences(rrule, dtstart, tz);
	printf("Number of occurrences = %d\n", no);
	next = dtstart;
	for (i = 0; i < no; i++) {
		next = get_occurrence(rrule, dtstart, tz, i + 1);
		printf("Next occurrence on %s", ctime(&next));
	}
	return;
}

/**
 * @brief
 *	print usage and wrapper function for test_it.
 *
 * @param[in] argc - num of args
 * @param[in] argv - argument list
 *
 * @return	int
 * @retval	0	success
 * #retval	1	failure
 *
 */
int
test_main(int argc, char *argv[])
{
	char *rrule;
	char *tz;
	time_t dtstart;
	int err_code;

	if (argc < 2) {
		printf("Usage: test_it <rrule>");
		return 1;
	}

	tz = getenv("PBS_TZID");
	dtstart = time(NULL);

	rrule = argv[1];
	/* apply to your local configuration */
	set_ical_zoneinfo("/usr/local/pbs/lib/ical/zoneinfo");
	test_it(rrule, dtstart, tz);
	printf("check_rrule returned %d\n", check_rrule(rrule, dtstart, 0, tz, &err_code));
	printf("get_num_occurrences = %d\n", get_num_occurrences(rrule, dtstart, tz));

	return 0;
}
#endif
