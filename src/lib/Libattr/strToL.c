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

#include <errno.h>
#include "Long.h"
#include "Long_.h"

#undef strToL
#undef strTouL
u_Long strTouL(const char *nptr, char **endptr, int base);
/**
 * @file	strToL.c
 *
 * @brief
 * 	strToL - returns the Long value representing the string whose first
 *	character is *nptr, when interpreted as an integer in base, base.
 */

/**
 * @brief
 * 	strToL - returns the Long value representing the string whose first
 *	character is *nptr, when interpreted as an integer in base, base.
 *
 * @param[in]   	nptr - pointer to string to convert to u_Long
 * @param[in/out]   	endptr -  If endptr is not NULL,the function stores
 *              		the address of the first invalid character in *endptr.
 * @param[in] 		base - If base is zero, the base of the integer is determined by the way the
 * 			string starts.  The string is interpreted as decimal if the first
 * 			character after leading white space and an optional sign is a digit
 * 			between 1 and 9, inclusive.  The string is interpreted as octal if the
 * 			first character after leading white space and an optional sign is the
 * 			digit "0" and the next character is not an "X" (either upper or lower
 * 			case).  The string is interpreted as hexidecimal if the first character
 * 			after leading white space and an optional sign is the digit "0",
 * 			followed by an "X" (either upper or lower case).
 *
 * 	If base is greater than 1 and less than the number of characters in the
 *	Long_dig array, it represents the base in which the number will be
 *	interpreted.  Characters for digits beyond 9 are represented by the
 *	letters of the alphabet, either upper case or lower case.
 *
 * @return Long Returns the result of the conversion
 * @retval >= 0 The result of the conversion
 * @retval 0    FAILURE
 *
 */

Long
strToL(const char *nptr, char **endptr, int base)
{
	Long value;

	value = (Long) strTouL(nptr, endptr, base);
	if (Long_neg) {
		if (value >= 0) {
			value = lONG_MIN;
			errno = ERANGE;
		}
	} else if (value < 0) {
		value = lONG_MAX;
		errno = ERANGE;
	}
	return (value);
}
