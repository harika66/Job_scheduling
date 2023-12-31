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

#if defined(FD_SET_IN_SYS_SELECT_H)
#include <sys/select.h>
#endif
#include <unistd.h>
#include <poll.h>
#include "libsec.h"
#include "libpbs.h"
#include "dis.h"
#include "auth.h"

volatile int reply_timedout = 0; /* for reply_send.c -- was alarm handler called? */

static int tcp_recv(int, void *, int);
static int tcp_send(int, void *, int);

/**
 * @brief
 *	Get the user buffer associated with the tcp channel. If no buffer has
 *	been set, then allocate a pbs_tcp_chan_t structure and associate with
 *	the given tcp channel
 *
 * @param[in] - fd - tcp channel to which to get/associate a user buffer
 *
 * @retval	NULL - Failure
 * @retval	!NULL - Buffer associated with the tcp channel
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
static pbs_tcp_chan_t *
tcp_get_chan(int fd)
{
	pbs_tcp_chan_t *chan = get_conn_chan(fd);
	if (chan == NULL) {
		if (errno != ENOTCONN) {
			dis_setup_chan(fd, get_conn_chan);
			chan = get_conn_chan(fd);
		}
	}
	return chan;
}

/**
 * @brief
 * 	tcp_recv - receive data from tcp stream
 *
 * @param[in] fd - socket descriptor
 * @param[out] data - data from tcp stream
 * @param[in] len - bytes to receive from tcp stream
 *
 * @return	int
 * @retval	0 	if success
 * @retval	-1 	if error
 * @retval	-2 	if EOF (stream closed)
 */
static int
tcp_recv(int fd, void *data, int len)
{
	int i = 0;
	int torecv = len;
	char *pb = (char *) data;
	int amt = 0;
#ifdef WIN32
	fd_set readset;
	struct timeval timeout;
#else
	struct pollfd pollfds[1];
	int timeout;
#endif

#ifdef WIN32
	timeout.tv_sec = (long) pbs_tcp_timeout;
	timeout.tv_usec = 0;
#else
	timeout = pbs_tcp_timeout;
	pollfds[0].fd = fd;
	pollfds[0].events = POLLIN;
	pollfds[0].revents = 0;
#endif

	while (torecv > 0) {
		/*
		 * we don't want to be locked out by an attack on the port to
		 * deny service, so we time out the read, the network had better
		 * deliver promptly
		 */
		do {
#ifdef WIN32
			FD_ZERO(&readset);
			FD_SET((unsigned int) fd, &readset);
			i = select(FD_SETSIZE, &readset, NULL, NULL, &timeout);
#else
			i = poll(pollfds, 1, timeout * 1000);
#endif
			if (pbs_tcp_interrupt)
				break;
		}
#ifdef WIN32
		while (i == -1 && ((errno = WSAGetLastError()) == WSAEINTR));
#else
		while (i == -1 && errno == EINTR);
#endif

		if (i <= 0)
			return i;

#ifdef WIN32
		i = recv(fd, pb, torecv, 0);
		errno = WSAGetLastError();
#else
		i = CS_read(fd, pb, torecv);
#endif
		if (i == 0)
			return -2;

		if (i < 0) {
#ifdef WIN32
			/*
			 * for WASCONNRESET, treat like no data for winsock
			 * will return this if remote
			 * connection prematurely closed
			 */
			if (errno == WSAECONNRESET)
				return 0;
			if (errno != WSAEINTR)
#else
			if (errno != EINTR)
#endif
				return i;
		} else {
			torecv -= i;
			pb += i;
			amt += i;
		}
	}
	return amt;
}

/**
 * @brief
 * 	tcp_send - send data to tcp stream
 *
 * @param[in] fd - socket descriptor
 * @param[out] data - data to send
 * @param[in] len - bytes to send
 *
 * @return	int
 * @retval	>0 	number of characters sent
 * @retval	0 	if EOD (no data currently avalable)
 * @retval	-1 	if error
 * @retval	-2 	if EOF (stream closed)
 */
static int
tcp_send(int fd, void *data, int len)
{
	size_t ct = (size_t) len;
	int i;
	int j;
	char *pb = (char *) data;
	struct pollfd pollfds[1];

#ifdef WIN32
	while ((i = send(fd, pb, (int) ct, 0)) != (int) ct) {
		errno = WSAGetLastError();
		if (i == -1) {
			if (errno != WSAEINTR) {
				pbs_tcp_errno = errno;
				return (-1);
			} else
				continue;
		}
#else
	while ((i = CS_write(fd, pb, ct)) != ct) {
		if (i == CS_IO_FAIL) {
			if (errno == EINTR) {
				continue;
			}
			if (errno != EAGAIN) {
				/* fatal error on write, abort output */
				pbs_tcp_errno = errno;
				return (-1);
			}

			/* write would have blocked (EAGAIN returned) */
			/* poll for socket to be ready to accept, if  */
			/* not ready in TIMEOUT_SHORT seconds, fail   */
			/* redo the poll if EINTR		      */
			do {
				if (reply_timedout) {
					/* caught alarm - timeout spanning several writes for one reply */
					/* alarm set up in dis_reply_write() */
					/* treat identically to poll timeout */
					j = 0;
					reply_timedout = 0;
				} else {
					pollfds[0].fd = fd;
					pollfds[0].events = POLLOUT;
					pollfds[0].revents = 0;
					j = poll(pollfds, 1, pbs_tcp_timeout * 1000);
				}
			} while ((j == -1) && (errno == EINTR));

			if (j == 0) {
				/* never came ready, return error */
				/* pbs_tcp_errno will add to log message */
				pbs_tcp_errno = EAGAIN;
				return (-1);
			} else if (j == -1) {
				/* some other error - fatal */
				pbs_tcp_errno = errno;
				return (-1);
			}
			continue; /* socket ready, retry write */
		}
#endif
		/* write succeeded, do more if needed */
		ct -= i;
		pb += i;
	}
	return len;
}

/**
 * @brief
 *	sets tcp related functions.
 *
 */
void
DIS_tcp_funcs()
{
	pfn_transport_get_chan = tcp_get_chan;
	pfn_transport_set_chan = set_conn_chan;
	pfn_transport_recv = tcp_recv;
	pfn_transport_send = tcp_send;
}
