/**
 * \file session.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to handle NETCONF sessions.
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>

#include <libssh2.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

#include "netconf_internal.h"
#include "messages.h"
#include "messages_internal.h"
#include "session.h"
#include "datastore.h"
#include "notifications.h"

extern struct nc_statistics *nc_stats;

struct session_list_s {
	struct nc_session *session;
	struct session_list_s *next;
};

struct session_list_s *session_list = NULL;

/**
 * Sleep time in microseconds to wait between unsuccessful reading due to EAGAIN or EWOULDBLOCK
 */
#define NC_READ_SLEEP 100
/*
 #define NC_WRITE(session,buf,c) \
	if(session->fd_output == STDOUT_FILENO){ \
		c += write (STDOUT_FILENO, (buf), strlen(buf)); \
	} else {\
		c += libssh2_channel_write (session->ssh_channel, (buf), strlen(buf));}
 */
#define NC_WRITE(session,buf,c) \
	if(session->ssh_channel){ \
		c += libssh2_channel_write (session->ssh_channel, (buf), strlen(buf)); \
	} else if (session->fd_output != -1) { \
		c += write (session->fd_output, (buf), strlen(buf));}

int nc_session_monitor(struct nc_session* session)
{
	struct session_list_s *list_item;

	if (session != NULL) {
		list_item = malloc (sizeof(struct session_list_s));
		if (list_item == NULL) {
			ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
		list_item->session = session;
		list_item->next = session_list;
		session_list = list_item;

		return (EXIT_SUCCESS);
	} else {
		return (EXIT_FAILURE);
	}
}

char* nc_session_stats(void)
{
	char *aux, *sessions = NULL, *session = NULL;
	struct session_list_s *list_item;

	for (list_item = session_list; list_item != NULL; list_item = list_item->next) {
		aux = NULL;
		asprintf(&aux, "<session><session-id>%s</session-id>"
				"<transport>netconf-ssh</transport>"
				"<username>%s</username>"
				"<source-host>%s</source-host>"
				"<login-time>%s</login-time>"
				"<in-rpcs>%u</in-rpcs><in-bad-rpcs>%u</in-bad-rpcs>"
				"<out-rpc-errors>%u</out-rpc-errors>"
				"<out-notifications>%u</out-notifications></session>",
				list_item->session->session_id,
				list_item->session->username,
				list_item->session->hostname,
				list_item->session->logintime,
				list_item->session->stats.in_rpcs,
				list_item->session->stats.in_bad_rpcs,
				list_item->session->stats.out_rpc_errors,
				list_item->session->stats.out_notifications);

		if (session == NULL) {
			session = aux;
		} else {
			session = realloc(session, strlen(session) + strlen(aux) + 1);
			strcat(session, aux);
		}
	}
	if (session != NULL) {
		asprintf(&sessions, "<sessions>%s</sessions>", session);
		free(session);
	}
	return (sessions);
}

const char* nc_session_get_id (const struct nc_session *session)
{
	if (session == NULL) {
		return (NULL);
	}
	return (session->session_id);
}

const char* nc_session_get_host(const struct nc_session* session)
{
	if (session == NULL) {
		return (NULL);
	}
	return (session->hostname);
}

const char* nc_session_get_port(const struct nc_session* session)
{
	if (session == NULL) {
		return (NULL);
	}
	return (session->port);
}

const char* nc_session_get_user(const struct nc_session* session)
{
	if (session == NULL) {
		return (NULL);
	}
	return (session->username);
}

int nc_session_get_version (const struct nc_session *session)
{
	if (session == NULL) {
		return (-1);
	}
	return (session->version);
}

int nc_session_get_eventfd (const struct nc_session *session)
{
	if (session == NULL) {
		return -1;
	}

	if (session->libssh2_socket != -1) {
		return (session->libssh2_socket);
	} else if (session->fd_input != -1) {
		return (session->fd_input);
	} else {
		return (-1);
	}
}

int nc_session_notif_allowed (const struct nc_session *session)
{
	if (session == NULL) {
		/* notification subscription is not allowed */
		return 0;
	}

	/* check capabilities */
	if (nc_cpblts_enabled(session, NC_CAP_NOTIFICATION_ID) == 1) {
		/* subscription is allowed only if another subscription is not active */
		return ((session->ntf_active == 0) ? 1 : 0);
	} else {
		/* notifications are not supported at all */
		return (0);
	}
}

void nc_cpblts_free(struct nc_cpblts *c)
{
	int i;

	if (c == NULL) {
		return;
	}

	if (c->list != NULL) {
		if (c->items <= c->list_size) {
			for(i = 0; i < c->items; i++) {
				if (c->list[i] != NULL) {
					free(c->list[i]);
				}
			}
		} else {
			WARN("nc_cpblts_free: invalid capabilities structure, some memory may not be freed.");
		}
		free(c->list);
	}
	free(c);
}

struct nc_cpblts *nc_cpblts_new(char* const* list)
{
	struct nc_cpblts *retval;
	char* item;
	int i;

	retval = calloc(1, sizeof(struct nc_cpblts));
	if (retval == NULL) {
		ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	retval->list_size = 10; /* initial value */
	retval->list = malloc(retval->list_size * sizeof(char*));
	if (retval->list == NULL) {
		ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		free(retval);
		return (NULL);
	}
	retval->list[0] = NULL;

	if (list != NULL) {
		for (i = 0, item = list[i]; item != NULL; i++) {
			retval->list[i] = strdup (item);
			retval->items++;
			if (retval->items == retval->list_size) {
				/* resize the capacity of the capabilities list */
				errno = 0;
				retval->list = realloc (retval->list, retval->list_size * 2 * sizeof (char*));
				if (errno != 0) {
					nc_cpblts_free (retval);
					return (NULL);
				}
				retval->list_size *= 2;
			}
			retval->list[i + 1] = NULL;
			item = list[i+1];
		}
	}

	return (retval);
}

int nc_cpblts_add (struct nc_cpblts *capabilities, const char* capability_string)
{
	int i;
	char *s, *p = NULL;

	if (capabilities == NULL || capability_string == NULL) {
		return (EXIT_FAILURE);
	}

	if (capabilities->items > capabilities->list_size) {
		WARN("nc_cpblts_add: structure inconsistency! Some data may be lost.");
		return (EXIT_FAILURE);
	}

	/* get working copy of capability_string where the parameters will be ignored */
	s = strdup(capability_string);
	if ((p = strchr(s, '?')) != NULL) {
		/* in following comparison, ignore capability's parameters */
		*p = 0;
	}

	/* find duplicities */
	for (i = 0; i < capabilities->items; i++) {
		if (strcmp(capabilities->list[i], s) == 0) {
			/* capability is already in the capabilities list, but
			 * parameters can differ, so substitute current instance
			 * with the new one
			 */
			free(capabilities->list[i]);
			if (p != NULL) {
				*p = '?';
			}
			capabilities->list[i] = s;
			return (EXIT_SUCCESS);
		}
	}
	/* unhide capability's parameters */
	if (p != NULL) {
		*p = '?';
	}

	capabilities->list[capabilities->items] = s;
	capabilities->items++;
	if (capabilities->items == capabilities->list_size) {
		/* resize the capacity of the capabilities list */
		errno = 0;
		capabilities->list = realloc(capabilities->list, capabilities->list_size * 2 * sizeof (char*));
		if (errno != 0) {
			return (EXIT_FAILURE);
		}
		capabilities->list_size *= 2;
	}
	capabilities->list[capabilities->items] = NULL;

	return (EXIT_SUCCESS);
}

int nc_cpblts_remove (struct nc_cpblts *capabilities, const char* capability_string)
{
	int i;
	char* s, *p;

	if (capabilities == NULL || capability_string == NULL) {
		return (EXIT_FAILURE);
	}

	if (capabilities->items > capabilities->list_size) {
		WARN("nc_cpblts_add: structure inconsistency! Some data may be lost.");
		return (EXIT_FAILURE);
	}

	s = strdup(capability_string);
	if ((p = strchr(s, '?')) != NULL) {
		/* in comparison, ignore capability's parameters */
		p = 0;
	}

	for (i = 0; i < capabilities->items; i++) {
		if (capabilities->list[i] != NULL && strncmp(capabilities->list[i], s, strlen(s)) == 0) {
			break;
		}
	}
	free(s);

	if (i < capabilities->items) {
		free(capabilities->list[i]);
		/* move here the last item from the list */
		capabilities->list[i] = capabilities->list[capabilities->items - 1];
		/* and then set the last item in the list to NULL */
		capabilities->list[capabilities->items - 1] = NULL;
		capabilities->items--;
	}

	return (EXIT_SUCCESS);
}

const char* nc_cpblts_get(const struct nc_cpblts *c, const char* capability_string)
{
	int i;
	char* s, *p;

	if (capability_string == NULL || c == NULL || c->list == NULL) {
		return (NULL);
	}

	s = strdup(capability_string);
	if ((p = strchr(s, '?')) != NULL) {
		/* in comparison, ignore capability's parameters */
		p = 0;
	}

	for (i = 0; c->list[i]; i++) {
		if (strncmp(s, c->list[i], strlen(s)) == 0) {
			free(s);
			return (c->list[i]);
		}
	}
	free(s);
	return (NULL);

}

int nc_cpblts_enabled(const struct nc_session* session, const char* capability_string)
{
	int i;
	char* s, *p;

	if (capability_string == NULL || session == NULL || session->capabilities == NULL) {
		return (0);
	}

	s = strdup(capability_string);
	if ((p = strchr(s, '?')) != NULL) {
		/* in comparison, ignore capability's parameters */
		p = 0;
	}

	for (i = 0; session->capabilities->list[i]; i++) {
		if (strncmp(s, session->capabilities->list[i], strlen(s)) == 0) {
			free(s);
			return (1);
		}
	}
	free(s);
	return (0);
}

void nc_cpblts_iter_start(struct nc_cpblts *c)
{
	if (c == NULL) {
		return;
	}
	c->iter = 0;
}

const char *nc_cpblts_iter_next(struct nc_cpblts *c)
{
	if (c == NULL || c->list == NULL) {
		return (NULL);
	}

	if (c->iter > (c->items - 1)) {
		return NULL;
	}

	return (c->list[c->iter++]);
}

int nc_cpblts_count(const struct nc_cpblts *c)
{
	if (c == NULL || c->list == NULL) {
		return 0;
	}
	return c->items;
}

struct nc_cpblts *nc_session_get_cpblts_default ()
{
	struct nc_cpblts *retval;

	retval = nc_cpblts_new(NULL);
	if (retval == NULL) {
		return (NULL);
	}

	nc_cpblts_add(retval, NC_CAP_BASE10_ID);
	nc_cpblts_add(retval, NC_CAP_BASE11_ID);
	nc_cpblts_add(retval, NC_CAP_WRUNNING_ID);
	nc_cpblts_add(retval, NC_CAP_CANDIDATE_ID);
	nc_cpblts_add(retval, NC_CAP_STARTUP_ID);
	nc_cpblts_add(retval, NC_CAP_NOTIFICATION_ID);
	nc_cpblts_add(retval, NC_CAP_INTERLEAVE_ID);
	nc_cpblts_add(retval, NC_CAP_MONITORING_ID);
	if (ncdflt_get_basic_mode() != NCDFLT_MODE_DISABLED) {
		nc_cpblts_add(retval, NC_CAP_WITHDEFAULTS_ID);
	}

	return (retval);
}

struct nc_cpblts* nc_session_get_cpblts (const struct nc_session* session)
{
	if (session == NULL) {
		return (NULL);
	}

	return (session->capabilities);
}

/**
 * @brief Parse with-defaults capability
 */
void parse_wdcap(struct nc_cpblts *capabilities, NCDFLT_MODE *basic, int *supported)
{
	const char* cpblt;
	char* s;

	if ((cpblt = nc_cpblts_get(capabilities, NC_CAP_WITHDEFAULTS_ID)) != NULL) {
		if ((s = strstr(cpblt, "report-all")) != NULL) {
			if (s[-1] == '=' && s[-2] == 'e') {
				/* basic mode: basic-mode=report-all */
				*basic = NCDFLT_MODE_ALL;
			}
			*supported = *supported | NCDFLT_MODE_ALL;
		}
		if ((s = strstr(cpblt, "trim")) != NULL) {
			if (s[-1] == '=' && s[-2] == 'e') {
				/* basic mode: basic-mode=trim */
				*basic = NCDFLT_MODE_TRIM;
			}
			*supported = *supported | NCDFLT_MODE_TRIM;
		}
		if ((s = strstr(cpblt, "explicit")) != NULL) {
			if (s[-1] == '=' && s[-2] == 'e') {
				/* basic mode: basic-mode=explicit */
				*basic = NCDFLT_MODE_EXPLICIT;
			}
			*supported = *supported | NCDFLT_MODE_EXPLICIT;
		}
		if ((s = strstr(cpblt, "report-all-tagged")) != NULL) {
			*supported = *supported | NCDFLT_MODE_ALL_TAGGED;
		}
	} else {
		*basic = NCDFLT_MODE_DISABLED;
		*supported = 0;
	}
}

struct nc_session* nc_session_dummy(const char* sid, const char* username, const char* hostname, struct nc_cpblts *capabilities)
{
	struct nc_session * session;
	const char* cpblt;

	if (sid == NULL || username == NULL || capabilities == NULL) {
		return NULL;
	}

	if ((session = malloc (sizeof (struct nc_session))) == NULL) {
		return NULL;
	}

	memset (session, 0, sizeof (struct nc_session));

	/* set invalid fd values to prevent comunication */
	session->fd_input = -1;
	session->fd_output = -1;
	session->libssh2_socket = -1;

	/* init stats values */
	session->logintime = nc_time2datetime(time(NULL));
	session->stats.in_rpcs = 0;
	session->stats.in_bad_rpcs = 0;
	session->stats.out_rpc_errors = 0;
	session->stats.out_notifications = 0;

	/*
	 * mutexes and queues fields are not initialized since dummy session
	 * can not send or receive any data
	 */

	/* session is DUMMY */
	session->status = NC_SESSION_STATUS_DUMMY;
	/* copy session id */
	strncpy (session->session_id, sid, SID_SIZE);
	/* copy user name */
	session->username = strdup (username);
	/* if specified, copy hostname */
	if (hostname != NULL) {
		session->hostname = strdup (hostname);
	}
	/* create empty capabilities list */
	session->capabilities = nc_cpblts_new (NULL);
	/* initialize capabilities iterator */
	nc_cpblts_iter_start (capabilities);
	/* copy all capabilities */
	while ((cpblt = nc_cpblts_iter_next (capabilities)) != NULL) {
		nc_cpblts_add (session->capabilities, cpblt);
	}

	session->wd_basic = NCDFLT_MODE_DISABLED;
	session->wd_modes = 0;
	/* set with defaults capability flags */
	parse_wdcap(session->capabilities, &(session->wd_basic), &(session->wd_modes));

	return session;
}

void nc_session_close(struct nc_session* session, NC_SESSION_TERM_REASON reason)
{
	int i;
	nc_rpc *rpc_close = NULL;
	nc_reply *reply = NULL;
	struct nc_msg *qmsg, *qmsg_aux;
	NC_SESSION_STATUS sstatus = session->status;

	/* lock session due to accessing its status and other items */
	if (sstatus != NC_SESSION_STATUS_DUMMY) {
		DBG_LOCK("mut_session");
		pthread_mutex_lock(&(session->mut_session));
	}

	/* close the SSH session */
	if (session != NULL && session->status != NC_SESSION_STATUS_CLOSING && session->status != NC_SESSION_STATUS_CLOSED) {

		/* log closing of the session */
		if (sstatus != NC_SESSION_STATUS_DUMMY) {
			ncntf_event_new(-1, NCNTF_BASE_SESSION_END, session, reason, NULL);
		}

		if (strcmp(session->session_id, INTERNAL_DUMMY_ID) != 0) {
			/*
			 * break all datastore locks held by the session,
			 * libnetconf's internal dummy sessions are excluded
			 */
			ncds_break_locks(session);
		}

		/* close ssh session */
		if (session->ssh_channel != NULL) {
			if (session->status == NC_SESSION_STATUS_WORKING && libssh2_channel_eof(session->ssh_channel) == 0) {
				/* prevent infinite recursion when socket is corrupted -> stack overflow */
				session->status = NC_SESSION_STATUS_CLOSING;

				/* close NETCONF session */
				rpc_close = nc_rpc_closesession();
				if (rpc_close != NULL) {
					if (nc_session_send_rpc(session, rpc_close) != 0) {
						nc_session_recv_reply(session, 10000, &reply); /* wait max 10 seconds */
						if (reply != NULL) {
							nc_reply_free(reply);
						}
					}
					if (rpc_close != NULL) {
						nc_rpc_free(rpc_close);
					}
				}
			}

			libssh2_channel_free(session->ssh_channel);
			session->ssh_channel = NULL;
		}

		if (session->ssh_session != NULL) {
			libssh2_session_disconnect(session->ssh_session, nc_session_term_string(reason));
			libssh2_session_free(session->ssh_session);
			session->ssh_session = NULL;
		}

		free(session->hostname);
		session->hostname = NULL;
		free(session->logintime);
		session->logintime = NULL;
		free(session->port);
		session->port = NULL;

		if (session->libssh2_socket != -1) {
			close(session->libssh2_socket);
			session->libssh2_socket = -1;
		}

		/* remove messages from the queues */
		for (i = 0, qmsg = session->queue_event; i < 2; i++, qmsg = session->queue_msg) {
			while (qmsg != NULL) {
				qmsg_aux = qmsg->next;
				nc_msg_free(qmsg);
				qmsg = qmsg_aux;
			}
		}

		/*
		 * username, capabilities and session_id are untouched
		 */

		/* successfully closed */
	}

	if (sstatus != NC_SESSION_STATUS_DUMMY) {
		session->status = NC_SESSION_STATUS_CLOSED;
		DBG_UNLOCK("mut_session");
		pthread_mutex_unlock(&(session->mut_session));
	} else {
		session->status = NC_SESSION_STATUS_CLOSED;

	}
}

void nc_session_free (struct nc_session* session)
{
	struct session_list_s *sitem, *sitem_pre = NULL;

	nc_session_close(session, NC_SESSION_TERM_OTHER);

	/* free items untouched by nc_session_close() */
	if (session->username != NULL) {
		free (session->username);
	}
	if (session->capabilities != NULL) {
		nc_cpblts_free(session->capabilities);
	}
	free(session->capabilities_original);

	/* destroy mutexes */
	pthread_mutex_destroy(&(session->mut_in));
	pthread_mutex_destroy(&(session->mut_out));
	pthread_mutex_destroy(&(session->mut_mqueue));
	pthread_mutex_destroy(&(session->mut_equeue));
	pthread_mutex_destroy(&(session->mut_session));

	/* remove from internal list if session is monitored */
	for(sitem = session_list; sitem != NULL; sitem = sitem->next) {
		if (session == sitem->session) {
			if (sitem_pre == NULL) {
				/* matching session is in the first item of the list */
				session_list = sitem->next;
			} else {
				/* we're somewhere in the middle of the list */
				sitem_pre = sitem->next;
			}
			free(sitem);
			break;
		}
		sitem_pre = sitem;
	}

	free (session);
}

NC_SESSION_STATUS nc_session_get_status (const struct nc_session* session)
{
	if (session == NULL) {
		return (NC_SESSION_STATUS_ERROR);
	}

	return (session->status);
}

int nc_session_send (struct nc_session* session, struct nc_msg *msg)
{
	ssize_t c = 0;
	int len;
	char *text;
	char buf[1024];

	if ((session->ssh_channel == NULL) && (session->fd_output == -1)) {
		return (EXIT_FAILURE);
	}

	/*
	 * maybe the previous check can be replaced by the following one, but
	 * using both cannot be wrong
	 */
	if (session->status != NC_SESSION_STATUS_WORKING &&
			session->status != NC_SESSION_STATUS_CLOSING) {
		return (EXIT_FAILURE);
	}

	/* lock the session for sending the data */
	DBG_LOCK("mut_out");
	pthread_mutex_lock(&(session->mut_out));

	xmlDocDumpFormatMemory (msg->doc, (xmlChar**) (&text), &len, NC_CONTENT_FORMATTED);
	DBG("Writing message: %s", text);

	/* if v1.1 send chunk information before message */
	if (session->version == NETCONFV11) {
		snprintf (buf, 1024, "\n#%d\n", (int) strlen (text));
		c = 0;
		do {
			NC_WRITE(session, &(buf[c]), c);
			if (c == LIBSSH2_ERROR_TIMEOUT) {
				DBG_UNLOCK("mut_out");
				pthread_mutex_unlock(&(session->mut_out));
				VERB("Writing data into the communication channel timeouted.");
				return (EXIT_FAILURE);
			}
		} while (c != strlen (buf));
	}

	/* write the message */
	c = 0;
	do {
		NC_WRITE(session, &(text[c]), c);
		if (c == LIBSSH2_ERROR_TIMEOUT) {
			DBG_UNLOCK("mut_out");
			pthread_mutex_unlock(&(session->mut_out));
			VERB("Writing data into the communication channel timeouted.");
			return (EXIT_FAILURE);
		}
	} while (c != strlen (text));
	free (text);

	/* close message */
	if (session->version == NETCONFV11) {
		text = NC_V11_END_MSG;
	} else { /* NETCONFV10 */
		text = NC_V10_END_MSG;
	}
	c = 0;
	do {
		NC_WRITE(session, &(text[c]), c);
		if (c == LIBSSH2_ERROR_TIMEOUT) {
			DBG_UNLOCK("mut_out");
			pthread_mutex_unlock(&(session->mut_out));
			VERB("Writing data into the communication channel timeouted.");
			return (EXIT_FAILURE);
		}
	} while (c != strlen (text));

	/* unlock the session's output */
	DBG_UNLOCK("mut_out");
	pthread_mutex_unlock(&(session->mut_out));

	return (EXIT_SUCCESS);
}

int nc_session_read_len (struct nc_session* session, size_t chunk_length, char **text, size_t *len)
{
	char *buf, *err_msg;
	ssize_t c;
	size_t rd = 0;

	/* check if we can work with the session */
	if (session->status != NC_SESSION_STATUS_WORKING &&
			session->status != NC_SESSION_STATUS_CLOSING) {
		return (EXIT_FAILURE);
	}

	buf = malloc ((chunk_length + 1) * sizeof(char));
	if (buf == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		*len = 0;
		*text = NULL;
		return (EXIT_FAILURE);
	}

	while (rd < chunk_length) {
		if (session->ssh_channel) {
			/* read via libssh2 */
			c = libssh2_channel_read(session->ssh_channel, &(buf[rd]), chunk_length - rd);
			if (c == LIBSSH2_ERROR_EAGAIN || c == LIBSSH2_ERROR_TIMEOUT) {
				usleep (NC_READ_SLEEP);
				continue;
			} else if (c < 0) {
				libssh2_session_last_error (session->ssh_session, &err_msg, NULL, 0);
				ERROR("Reading from SSH channel failed (%ld: %s)", c, err_msg);
				free (buf);
				*len = 0;
				*text = NULL;
				return (EXIT_FAILURE);
			} else if (c == 0) {
				if (libssh2_channel_eof (session->ssh_channel)) {
					ERROR("Server has closed the communication socket");
					free (buf);
					*len = 0;
					*text = NULL;
					return (EXIT_FAILURE);
				}
				usleep (NC_READ_SLEEP);
				continue;
			}
		} else if (session->fd_input != -1) {
			/* read via file descriptor */
			c = read (session->fd_input, &(buf[rd]), chunk_length - rd);
			if (c == -1) {
				if (errno == EAGAIN) {
					usleep (NC_READ_SLEEP);
					continue;
				} else {
					ERROR("Reading from input file descriptor failed (%s)", strerror(errno));
					free (buf);
					*len = 0;
					*text = NULL;
					return (EXIT_FAILURE);
				}
			}
		} else {
			ERROR("No way to read input, fatal error.");
			free (buf);
			*len = 0;
			*text = NULL;
			return (EXIT_FAILURE);
		}

		rd += c;
	}

	/* add terminating null byte */
	buf[rd] = 0;

	*len = rd;
	*text = buf;
	return (EXIT_SUCCESS);
}

int nc_session_read_until (struct nc_session* session, const char* endtag, char **text, size_t *len)
{
	char *err_msg;
	size_t rd = 0;
	ssize_t c;
	static char *buf = NULL;
	static int buflen = 0;

	/* check if we can work with the session */
	if (session->status != NC_SESSION_STATUS_WORKING &&
			session->status != NC_SESSION_STATUS_CLOSING) {
		return (EXIT_FAILURE);
	}

	if (endtag == NULL) {
		return (EXIT_FAILURE);
	}

	/* allocate memory according to so far maximum buffer size */
	if (buflen == 0) {
		/* set starting buffer size */
		buflen = 1024;
		buf = (char*) malloc (buflen * sizeof(char));
		if (buf == NULL) {
			ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
	}

	for (rd = 0;;) {
		if (session->ssh_channel) {
			/* read via libssh2 */
			c = libssh2_channel_read(session->ssh_channel, &(buf[rd]), 1);
			if (c == LIBSSH2_ERROR_EAGAIN || c == LIBSSH2_ERROR_TIMEOUT) {
				usleep (NC_READ_SLEEP);
				continue;
			} else if (c < 0) {
				libssh2_session_last_error (session->ssh_session, &err_msg, NULL, 0);
				ERROR("Reading from SSH channel failed (%ld: %s)", c, err_msg);
				free (buf);
				buflen = 0;
				if (len != NULL) {
					*len = 0;
				}
				if (text != NULL) {
					*text = NULL;
				}
				return (EXIT_FAILURE);
			} else if (c == 0) {
				if (libssh2_channel_eof (session->ssh_channel)) {
					ERROR("Server has closed the communication socket");
					free (buf);
					buflen = 0;
					if (len != NULL) {
						*len = 0;
					}
					if (text != NULL) {
						*text = NULL;
					}
					return (EXIT_FAILURE);
				}
				usleep (NC_READ_SLEEP);
				continue;
			}
		} else if (session->fd_input != -1) {
			/* read via file descriptor */
			c = read (session->fd_input, &(buf[rd]), 1);
			if (c == -1) {
				if (errno == EAGAIN) {
					usleep (NC_READ_SLEEP);
					continue;
				} else {
					ERROR("Reading from input file descriptor failed (%s)", strerror(errno));
					free (buf);
					buflen = 0;
					if (len != NULL) {
						*len = 0;
					}
					if (text != NULL) {
						*text = NULL;
					}
					return (EXIT_FAILURE);
				}
			}
		} else {
			ERROR("No way to read input, fatal error.");
			free (buf);
			buflen = 0;
			if (len != NULL) {
				*len = 0;
			}
			if (text != NULL) {
				*text = NULL;
			}
			return (EXIT_FAILURE);
		}

		rd += c; /* should be rd++ */
		buf[rd] = '\0';

		if ((rd) < strlen (endtag)) {
			/* not enough data to compare with endtag */
			continue;
		} else {
			/* compare with endtag */
			if (strcmp (endtag, &(buf[rd - strlen (endtag)])) == 0) {
				/* end tag found */
				if (len != NULL) {
					*len = rd;
				}
				if (text != NULL) {
					*text = strdup (buf);
				}
				return (EXIT_SUCCESS);
			}
		}

		/* resize buffer if needed */
		if (rd == (buflen-1)) {
			/* get more memory for the text */
			buf = (char*) realloc (buf, (2 * buflen) * sizeof(char));
			if (buf == NULL) {
				ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
				if (len != NULL) {
					*len = 0;
				}
				if (text != NULL) {
					*text = NULL;
				}
				return (EXIT_FAILURE);
			}
			buflen = 2 * buflen;
		}
	}

	/* no one could be here */
	ERROR("Reading NETCONF message fatal failure");
	if (buf != NULL) {
		free (buf);
		buflen = 0;
	}
	if (len != NULL) {
		*len = 0;
	}
	if (text != NULL) {
		*text = NULL;
	}
	return (EXIT_FAILURE);
}

/**
 * @brief Get message id string from the NETCONF message
 *
 * @param[in] msg NETCONF message to parse.
 * @return 0 on error,\n message-id of the message on success.
 */
const nc_msgid nc_msg_parse_msgid(const struct nc_msg *msg)
{
	nc_msgid ret = NULL;
	xmlAttrPtr prop;

	/* parse and store message-id */
	prop = xmlHasProp(msg->doc->children, BAD_CAST "message-id");
	if (prop != NULL && prop->children != NULL && prop->children->content != NULL) {
		ret = (char*)prop->children->content;
	}
	if (ret == NULL) {
		if (xmlStrcmp (msg->doc->children->name, BAD_CAST "hello") != 0) {
			WARN("Missing message-id in %s.", (char*)msg->doc->children->name);
			ret = NULL;
		} else {
			ret = "hello";
		}
	}

	return (ret);
}

struct nc_err* nc_msg_parse_error(struct nc_msg* msg)
{
	struct nc_err* err;
	xmlNodePtr node, tmp;

	if (msg == NULL || msg->doc == NULL) {
		ERROR ("libnetconf internal error, invalid NETCONF message structure to parse.");
		return (NULL);
	}

	err = calloc(1, sizeof(struct nc_err));
	if (err == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (xmlStrcmp (msg->doc->children->children->name, BAD_CAST "rpc-error") != 0) {
		ERROR("%s: Given message is not rpc-error.", __func__);
		return (NULL);
	}

	for (node = msg->doc->children->children->children; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			/* skip comment nodes and others */
			continue;
		}

		if (xmlStrEqual(node->name, BAD_CAST "error-tag")) {
			err->tag = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual(node->name, BAD_CAST "error-type")) {
			err->type = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual(node->name, BAD_CAST "error-severity")) {
			err->severity = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual(node->name, BAD_CAST "error-app-tag")) {
			err->apptag = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual(node->name, BAD_CAST "error-path")) {
			err->path = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual(node->name, BAD_CAST "error-message")) {
			err->message = (char*)xmlNodeGetContent(node);
		} else if (xmlStrEqual (node->name, BAD_CAST "error-info")) {
			tmp = node->children;
			while (tmp) {
				if (xmlStrEqual(tmp->name, BAD_CAST "bad-attribute")) {
					err->attribute = (char*)xmlNodeGetContent(tmp);
				} else if (xmlStrEqual(tmp->name, BAD_CAST "bad-element")) {
					err->element = (char*)xmlNodeGetContent(tmp);
				} else if (xmlStrEqual(tmp->name, BAD_CAST "session-id")) {
					err->sid = (char*)xmlNodeGetContent(tmp);
				} else if (xmlStrEqual(tmp->name, BAD_CAST "bad-namespace")) {
					err->ns = (char*)xmlNodeGetContent(tmp);
				}
				tmp = tmp->next;
			}
		} else {
			WARN("Unknown element %s while parsing rpc-error.", (char*)(node->name));
		}
	}

	return (err);
}

NC_MSG_TYPE nc_session_receive (struct nc_session* session, int timeout, struct nc_msg** msg)
{
	struct nc_msg *retval;
	nc_reply* reply;
	const char* id;
	char *text = NULL, *chunk = NULL;
	size_t len;
	unsigned long long int text_size = 0, total_len = 0;
	size_t chunk_length;
	struct pollfd fds;
	LIBSSH2_POLLFD fds_ssh;
	int status;
	unsigned long int revents;
	NC_MSG_TYPE msgtype;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to receive data.");
		return (NC_MSG_UNKNOWN);
	}

	/* lock the session for receiving */
	DBG_LOCK("mut_in");
	pthread_mutex_lock(&(session->mut_in));

	/* use while for possibility of repeating test */
	while(1) {
		if (session->ssh_channel == NULL && session->fd_input != -1) {
			/* we are getting data from standard file descriptor */
			fds.fd = session->fd_input;
			fds.events = POLLIN;
			fds.revents = 0;
			status = poll(&fds, 1, timeout);

			revents = (unsigned long int) fds.revents;
		} else if (session->ssh_channel != NULL) {
			/* we are getting data from libssh's channel */
			fds_ssh.type = LIBSSH2_POLLFD_CHANNEL;
			fds_ssh.fd.channel = session->ssh_channel;
			fds_ssh.events = LIBSSH2_POLLFD_POLLIN;
			fds_ssh.revents = LIBSSH2_POLLFD_POLLIN;
			/*
			 * According to libssh2 documentation, standard poll should work, but it does not.
			 * It seems, that some data are stored in internal buffers and they are not seen
			 * by poll, but libssh2_poll on the channel.
			 */
			/*
			fds.fd = eventfd;
			fds.events = POLLIN;
			fds.revents = 0;
			status = poll(&fds, 1, 100);
			*/
			status = libssh2_poll(&fds_ssh, 1, timeout);

			revents = fds_ssh.revents;
		}

		/* process the result */
		if (status == 0) {
			/* timed out */
			DBG_UNLOCK("mut_in");
			pthread_mutex_unlock(&(session->mut_in));
			return (NC_MSG_WOULDBLOCK);
		} else if ((status == -1) && (errno == EINTR)) {
			/* poll was interrupted */
			continue;
		} else if (status < 0) {
			/* poll failed - something wrong happend, close this socket and wait for another request */
			ERROR("Input channel error");
			nc_session_close(session, NC_SESSION_TERM_DROPPED);
			DBG_UNLOCK("mut_in");
			pthread_mutex_unlock(&(session->mut_in));
			if (nc_stats) {nc_stats->sessions_dropped++;}
			return (NC_MSG_UNKNOWN);

		}
		/* status > 0 */
		/* check the status of the socket */
		/* if nothing to read and POLLHUP (EOF) or POLLERR set */
		if ((revents & POLLHUP) || (revents & POLLERR)) {
			/* close client's socket (it's probably already closed by client */
			ERROR("Input channel closed");
			nc_session_close(session, NC_SESSION_TERM_DROPPED);
			DBG_UNLOCK("mut_in");
			pthread_mutex_unlock(&(session->mut_in));
			if (nc_stats) {nc_stats->sessions_dropped++;}
			return (NC_MSG_UNKNOWN);
		}

		/* we have something to read */
		break;
	}

	switch (session->version) {
	case NETCONFV10:
		if (nc_session_read_until (session, NC_V10_END_MSG, &text, &len) != 0) {
			goto malformed_msg;
		}
		text[len - strlen (NC_V10_END_MSG)] = 0;
		DBG("Received message: %s", text);
		break;
	case NETCONFV11:
		do {
			if (nc_session_read_until (session, "\n#", NULL, NULL) != 0) {
				if (total_len > 0) {
					free (text);
				}
				goto malformed_msg;
			}
			if (nc_session_read_until (session, "\n", &chunk, &len) != 0) {
				if (total_len > 0) {
					free (text);
				}
				goto malformed_msg;
			}
			if (strcmp (chunk, "#\n") == 0) {
				/* end of chunked framing message */
				free (chunk);
				break;
			}

			/* convert string to the size of the following chunk */
			chunk_length = strtoul (chunk, (char **) NULL, 10);
			if (chunk_length == 0) {
				ERROR("Invalid frame chunk size detected, fatal error.");
				goto malformed_msg;
			}
			free (chunk);
			chunk = NULL;

			/* now we have size of next chunk, so read the chunk */
			if (nc_session_read_len (session, chunk_length, &chunk, &len) != 0) {
				if (total_len > 0) {
					free (text);
				}
				goto malformed_msg;
			}

			/* realloc resulting text buffer if needed (always needed now) */
			if (text_size < (total_len + len + 1)) {
				text = realloc (text, total_len + len + 1);
				if (text == NULL) {
					ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
					goto malformed_msg;
				}
				text[total_len] = '\0';
				text_size = total_len + len + 1;
			}
			strcat (text, chunk);
			total_len = strlen (text); /* don't forget count terminating null byte */
			free (chunk);
			chunk = NULL;

		} while (1);
		DBG("Received message: %s", text);
		break;
	default:
		ERROR("Unsupported NETCONF protocol version (%d)", session->version);
		goto malformed_msg;
		break;
	}

	DBG_UNLOCK("mut_in");
	pthread_mutex_unlock(&(session->mut_in));

	retval = malloc (sizeof(struct nc_msg));
	if (retval == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		free (text);
		goto malformed_msg;
	}
	retval->error = NULL;
	retval->next = NULL;

	/* store the received message in libxml2 format */
	retval->doc = xmlReadDoc (BAD_CAST text, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (retval->doc == NULL) {
		free (retval);
		free (text);
		ERROR("Invalid XML data received.");
		goto malformed_msg;
	}
	free (text);

	/* parse and store rpc-reply type */
	if (xmlStrcmp (retval->doc->children->name, BAD_CAST "rpc-reply") == 0) {
		msgtype = NC_MSG_REPLY;
		if (xmlStrcmp (retval->doc->children->children->name, BAD_CAST "ok") == 0) {
			retval->type.reply = NC_REPLY_OK;
		} else if (xmlStrcmp (retval->doc->children->children->name, BAD_CAST "rpc-error") == 0) {
			retval->type.reply = NC_REPLY_ERROR;
			retval->error = nc_msg_parse_error(retval);
		} else if (xmlStrcmp (retval->doc->children->children->name, BAD_CAST "data") == 0) {
			retval->type.reply = NC_REPLY_DATA;
		} else {
			retval->type.reply = NC_REPLY_UNKNOWN;
			WARN("Unknown type of received <rpc-reply> detected.");
		}
	} else if (xmlStrcmp (retval->doc->children->name, BAD_CAST "rpc") == 0) {
		msgtype = NC_MSG_RPC;
		if ((xmlStrcmp (retval->doc->children->children->name, BAD_CAST "get") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "get-schema") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "get-config") == 0)) {
			retval->type.rpc = NC_RPC_DATASTORE_READ;
		} else if ((xmlStrcmp (retval->doc->children->children->name, BAD_CAST "copy-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "delete-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "edit-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "lock") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "unlock") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "commit") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "discard-changes") == 0)) {
			retval->type.rpc = NC_RPC_DATASTORE_WRITE;
		} else if ((xmlStrcmp (retval->doc->children->children->name, BAD_CAST "kill-session") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "close-session") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "create-subscription") == 0)){
			retval->type.rpc = NC_RPC_SESSION;
		} else {
			retval->type.rpc = NC_RPC_UNKNOWN;
		}
	} else if (xmlStrcmp (retval->doc->children->name, BAD_CAST "notification") == 0) {
		/* we have notification */
		msgtype = NC_MSG_NOTIFICATION;
	} else if (xmlStrcmp (retval->doc->children->name, BAD_CAST "hello") == 0) {
		/* set message type, we have <hello> message */
		retval->type.reply = NC_REPLY_HELLO;
		msgtype = NC_MSG_HELLO;
	} else {
		WARN("Unknown (unsupported) type of received message detected.");
		retval->type.rpc = NC_RPC_UNKNOWN;
		msgtype = NC_MSG_UNKNOWN;
	}

	if (msgtype == NC_MSG_RPC || msgtype == NC_MSG_REPLY) {
		/* parse and store message-id */
		if ((id = nc_msg_parse_msgid(retval)) == NULL) {
			retval->msgid = NULL;
		} else {
			retval->msgid = strdup(id);
		}
	} else {
		retval->msgid = NULL;
	}

	/* return the result */
	*msg = retval;
	return (msgtype);

malformed_msg:
	if (session->version == NETCONFV11 && session->ssh_session == NULL) {
		/* NETCONF version 1.1 define sending error reply from the server */
		reply = nc_reply_error(nc_err_new(NC_ERR_MALFORMED_MSG));
		if (reply == NULL) {
			ERROR("Unable to create \'Malformed message\' reply");
			nc_session_close(session, NC_SESSION_TERM_OTHER);
			return (NC_MSG_UNKNOWN);
		}

		nc_session_send_reply(session, NULL, reply);
		nc_reply_free(reply);
	}

	ERROR("Malformed message received, closing the session %s.", session->session_id);
	nc_session_close(session, NC_SESSION_TERM_OTHER);

	return (NC_MSG_UNKNOWN);
}

NC_MSG_TYPE nc_session_recv_msg (struct nc_session* session, int timeout, struct nc_msg** msg)
{
	NC_MSG_TYPE ret;

	ret = nc_session_receive (session, timeout, msg);
	switch (ret) {
	case NC_MSG_REPLY: /* regular reply received */
	case NC_MSG_HELLO:
	case NC_MSG_NOTIFICATION:
	case NC_MSG_WOULDBLOCK:
		/* do nothing, just return the type */
		break;
	default:
		ret = NC_MSG_UNKNOWN;
		break;
	}

	return (ret);
}

NC_MSG_TYPE nc_session_recv_reply (struct nc_session* session, int timeout, nc_reply** reply)
{
	struct nc_msg *msg_aux, *msg;
	NC_MSG_TYPE ret;
	int local_timeout;

	if (timeout == 0) {
		local_timeout = 0;
	} else {
		local_timeout = 100;
	}

	DBG_LOCK("mut_mqueue");
	pthread_mutex_lock(&(session->mut_mqueue));

try_again:
	if (session->queue_msg != NULL) {
		/* pop the oldest reply from the queue */
		*reply = (nc_reply*)(session->queue_msg);
		session->queue_msg = (*reply)->next;
		DBG_UNLOCK("mut_mqueue");
		pthread_mutex_unlock(&(session->mut_mqueue));
		(*reply)->next = NULL;
		return (NC_MSG_REPLY);
	}

	ret = nc_session_recv_msg(session, local_timeout, &msg);

	switch (ret) {
	case NC_MSG_REPLY: /* regular reply received */
		/* if specified callback for processing rpc-error, use it */
		if (nc_reply_get_type (msg) == NC_REPLY_ERROR &&
				callbacks.process_error_reply != NULL) {
			/* process rpc-error msg */
			callbacks.process_error_reply(msg->error->tag,
					msg->error->type,
					msg->error->severity,
					msg->error->apptag,
					msg->error->path,
					msg->error->message,
					msg->error->attribute,
					msg->error->element,
					msg->error->ns,
					msg->error->sid);
			/* free the data */
			nc_reply_free(msg);
			ret = NC_MSG_NONE;
		} else {
			*reply = (nc_reply*)msg;
		}
		break;
	case NC_MSG_HELLO: /* hello message received */
		*reply = (nc_reply*)msg;
		break;
	case NC_MSG_NONE:
		/* <rpc-reply> with error information was processed automatically */
		break;
	case NC_MSG_WOULDBLOCK:
		if ((timeout == -1) || ((timeout > 0) && ((local_timeout = local_timeout - 100) > 0))) {
			goto try_again;
		}
		break;
	case NC_MSG_NOTIFICATION:
		/* add event notification into the session's list of notification messages */
		msg_aux = session->queue_event;
		if (msg_aux == NULL) {
			msg->next = session->queue_event;
			session->queue_event = msg;
		} else {
			for (; msg_aux->next != NULL; msg_aux = msg_aux->next);
			msg_aux->next = msg;
		}

		break;
	default:
		ret = NC_MSG_UNKNOWN;
		break;
	}

	/* session lock is no more needed */
	DBG_UNLOCK("mut_mqueue");
	pthread_mutex_unlock(&(session->mut_mqueue));

	return (ret);
}

int nc_session_send_notif (struct nc_session* session, const nc_ntf* ntf)
{
	int ret;
	struct nc_msg *msg;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to send <notification>.");
		return (EXIT_FAILURE);
	}

	msg = nc_msg_dup ((struct nc_msg*) ntf);

	/* set proper namespace according to NETCONF version */
	xmlNewNs (msg->doc->children, BAD_CAST NC_NS_BASE10, NULL);

	/* send message */
	ret = nc_session_send (session, msg);

	nc_msg_free (msg);

	if (ret == EXIT_SUCCESS) {
		/* update stats */
		session->stats.out_notifications++;
		if (nc_stats) {nc_stats->counters.out_notifications++;}
	}

	return (ret);
}

NC_MSG_TYPE nc_session_recv_notif (struct nc_session* session, int timeout, nc_ntf** ntf)
{
	struct nc_msg *msg_aux, *msg;
	NC_MSG_TYPE ret;
	int local_timeout;

	if (timeout == 0) {
		local_timeout = 0;
	} else {
		local_timeout = 100;
	}

	DBG_LOCK("mut_equeue");
	pthread_mutex_lock(&(session->mut_equeue));

try_again:
	if (session->queue_event != NULL) {
		/* pop the oldest reply from the queue */
		*ntf = (nc_reply*)(session->queue_event);
		session->queue_event = (*ntf)->next;
		DBG_UNLOCK("mut_equeue");
		pthread_mutex_unlock(&(session->mut_equeue));
		(*ntf)->next = NULL;
		return (NC_MSG_NOTIFICATION);
	}

	ret = nc_session_recv_msg(session, local_timeout, &msg);

	switch (ret) {
	case NC_MSG_REPLY: /* regular reply received */
		/* add reply into the session's list of reply messages */
		msg_aux = session->queue_msg;
		if (msg_aux == NULL) {
			msg->next = session->queue_msg;
			session->queue_msg = msg;
		} else {
			for (; msg_aux->next != NULL; msg_aux = msg_aux->next);
			msg_aux->next = msg;
		}
		break;
	case NC_MSG_NONE:
		/* <rpc-reply> with error information was processed
		 * automatically, but we are waiting for notification
		 */
		break;
	case NC_MSG_WOULDBLOCK:
		if ((timeout == -1) || ((timeout > 0) && ((local_timeout = local_timeout - 100) > 0))) {
			goto try_again;
		}
		break;
	case NC_MSG_NOTIFICATION:
		*ntf = (nc_reply*)msg;
		break;
	default:
		ret = NC_MSG_UNKNOWN;
		break;
	}

	DBG_UNLOCK("mut_equeue");
	pthread_mutex_unlock(&(session->mut_equeue));

	return (ret);
}

NC_MSG_TYPE nc_session_recv_rpc (struct nc_session* session, int timeout, nc_rpc** rpc)
{
	NC_MSG_TYPE ret;
	const char* wd;
	struct nc_err* e = NULL;
	nc_reply* reply;
	int local_timeout;

	if (timeout == 0) {
		local_timeout = 0;
	} else {
		local_timeout = 100;
	}

try_again:
	ret = nc_session_receive (session, local_timeout, (struct nc_msg**) rpc);
	switch (ret) {
	case NC_MSG_RPC:
		(*rpc)->with_defaults = nc_rpc_parse_withdefaults(*rpc);

		/* check for with-defaults capability */
		if ((*rpc)->with_defaults != NCDFLT_MODE_DISABLED) {
			/* check if the session support this */
			if ((wd = nc_cpblts_get(session->capabilities, NC_CAP_WITHDEFAULTS_ID)) == NULL) {
				ERROR("rpc requires with-defaults capability, but session does not support it.");
				e = nc_err_new(NC_ERR_INVALID_VALUE);
				nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
				nc_err_set(e, NC_ERR_PARAM_MSG, "rpc requires with-defaults capability, but session does not support it.");
			} else {
				switch ((*rpc)->with_defaults) {
				case NCDFLT_MODE_ALL:
					if (strstr(wd, "report-all") == NULL) {
						ERROR("rpc requires with-defaults capability report-all mode, but session does not support it.");
						e = nc_err_new(NC_ERR_INVALID_VALUE);
						nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
						nc_err_set(e, NC_ERR_PARAM_MSG, "rpc requires with-defaults capability report-all mode, but session does not support it.");
					}
					break;
				case NCDFLT_MODE_ALL_TAGGED:
					if (strstr(wd, "report-all-tagged") == NULL) {
						ERROR("rpc requires with-defaults capability report-all-tagged mode, but session does not support it.");
						e = nc_err_new(NC_ERR_INVALID_VALUE);
						nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
						nc_err_set(e, NC_ERR_PARAM_MSG, "rpc requires with-defaults capability report-all-tagged mode, but session does not support it.");
					}
					break;
				case NCDFLT_MODE_TRIM:
					if (strstr(wd, "trim") == NULL) {
						ERROR("rpc requires with-defaults capability trim mode, but session does not support it.");
						e = nc_err_new(NC_ERR_INVALID_VALUE);
						nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
						nc_err_set(e, NC_ERR_PARAM_MSG, "rpc requires with-defaults capability trim mode, but session does not support it.");
					}
					break;
				case NCDFLT_MODE_EXPLICIT:
					if (strstr(wd, "explicit") == NULL) {
						ERROR("rpc requires with-defaults capability explicit mode, but session does not support it.");
						e = nc_err_new(NC_ERR_INVALID_VALUE);
						nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
						nc_err_set(e, NC_ERR_PARAM_MSG, "rpc requires with-defaults capability explicit mode, but session does not support it.");
					}
					break;
				default: /* something weird */
					ERROR("rpc requires with-defaults capability with unknown mode.");
					e = nc_err_new(NC_ERR_INVALID_VALUE);
					nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "with-defaults");
					nc_err_set(e, NC_ERR_PARAM_MSG, "pc requires with-defaults capability with unknown mode.");
					break;
				}
			}

			if (e != NULL) {
				reply = nc_reply_error(e);
				nc_session_send_reply(session, *rpc, reply);
				nc_rpc_free(*rpc);
				*rpc = NULL;
				nc_reply_free(reply);

				/* update stats */
				session->stats.in_bad_rpcs++;
				if (nc_stats) {nc_stats->counters.in_bad_rpcs++;}

				return (0); /* failure */
			}
		}
		/* update statistics */
		session->stats.in_rpcs++;
		if (nc_stats) {nc_stats->counters.in_rpcs++;}

		break;
	case NC_MSG_HELLO:
		/* do nothing, just return the type */
		break;
	case NC_MSG_WOULDBLOCK:
		if ((timeout == -1) || ((timeout > 0) && ((local_timeout = local_timeout - 100) > 0))) {
			goto try_again;
		}
		break;
	default:
		ret = NC_MSG_UNKNOWN;

		/* update stats */
		session->stats.in_bad_rpcs++;
		if (nc_stats) {nc_stats->counters.in_bad_rpcs++;}

		break;
	}

	return (ret);
}

const nc_msgid nc_session_send_rpc (struct nc_session* session, nc_rpc *rpc)
{
	int ret;
	char msg_id_str[16];
	const char* wd;
	struct nc_msg *msg;
	NC_OP op;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to send <rpc>.");
		return (NULL); /* failure */
	}

	if (rpc->type.rpc != NC_RPC_HELLO) {
		/* check for capabilities operations */
		op = nc_rpc_get_op(rpc);
		/* :notifications */
		switch (op) {
		case NC_OP_CREATESUBSCRIPTION:
			if (nc_cpblts_enabled(session, NC_CAP_NOTIFICATION_ID) == 0) {
				ERROR("RPC requires :notifications capability, but session does not support it.");
				return (NULL); /* failure */
			}
			break;
		case NC_OP_COMMIT:
		case NC_OP_DISCARDCHANGES:
			if (nc_cpblts_enabled(session, NC_CAP_CANDIDATE_ID) == 0) {
				ERROR("RPC requires :candidate capability, but session does not support it.");
				return (NULL); /* failure */
			}
			break;
		case NC_OP_GETSCHEMA:
			if (nc_cpblts_enabled(session, NC_CAP_MONITORING_ID) == 0) {
				ERROR("RPC requires :monitoring capability, but session does not support it.");
				return (NULL); /* failure */
			}
			break;
		default:
			/* no check is needed */
			break;
		}

		/* check for with-defaults capability */
		if (rpc->with_defaults != NCDFLT_MODE_DISABLED) {
			/* check if the session support this */
			if ((wd = nc_cpblts_get(session->capabilities, NC_CAP_WITHDEFAULTS_ID)) == NULL) {
				ERROR("RPC requires :with-defaults capability, but session does not support it.");
				return (NULL); /* failure */
			}
			switch (rpc->with_defaults) {
			case NCDFLT_MODE_ALL:
				if (strstr(wd, "report-all") == NULL) {
					ERROR("RPC requires with-defaults capability report-all mode, but session does not support it.");
					return (NULL); /* failure */
				}
				break;
			case NCDFLT_MODE_ALL_TAGGED:
				if (strstr(wd, "report-all-tagged") == NULL) {
					ERROR("RPC requires with-defaults capability report-all-tagged mode, but session does not support it.");
					return (NULL); /* failure */
				}
				break;
			case NCDFLT_MODE_TRIM:
				if (strstr(wd, "trim") == NULL) {
					ERROR("RPC requires with-defaults capability trim mode, but session does not support it.");
					return (NULL); /* failure */
				}
				break;
			case NCDFLT_MODE_EXPLICIT:
				if (strstr(wd, "explicit") == NULL) {
					ERROR("RPC requires with-defaults capability explicit mode, but session does not support it.");
					return (NULL); /* failure */
				}
				break;
			default: /* NCDFLT_MODE_DISABLED */
				/* nothing to check */
				break;
			}
		}
	}

	msg = nc_msg_dup ((struct nc_msg*) rpc);
	/* set message id */
	if (xmlStrcmp (msg->doc->children->name, BAD_CAST "rpc") == 0) {
		/* lock the session due to accessing msgid item */
		DBG_LOCK("mut_session");
		pthread_mutex_lock(&(session->mut_session));
		sprintf (msg_id_str, "%llu", session->msgid++);
		DBG_UNLOCK("mut_session");
		pthread_mutex_unlock(&(session->mut_session));
		xmlSetProp (msg->doc->children, BAD_CAST "message-id", BAD_CAST msg_id_str);
	} else {
		/* hello message */
		sprintf (msg_id_str, "hello");
	}

	/* set proper namespace according to NETCONF version */
	xmlNewNs (msg->doc->children, BAD_CAST NC_NS_BASE10, NULL);

	/* send message */
	ret = nc_session_send (session, msg);

	nc_msg_free (msg);

	if (ret != EXIT_SUCCESS) {
		if (rpc->type.rpc != NC_RPC_HELLO) {
			DBG_LOCK("mut_session");
			pthread_mutex_lock(&(session->mut_session));
			session->msgid--;
			DBG_UNLOCK("mut_session");
			pthread_mutex_unlock(&(session->mut_session));
		}
		return (NULL);
	} else {
		rpc->msgid = strdup(msg_id_str);
		return (rpc->msgid);
	}
}

const nc_msgid nc_session_send_reply (struct nc_session* session, const nc_rpc* rpc, const nc_reply *reply)
{
	int ret;
	struct nc_msg *msg;
	nc_msgid retval = NULL;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to send <rpc-reply>.");
		return (0); /* failure */
	}

	if (rpc == NULL) {
		ERROR("%s: Invalid <rpc> message to answer.", __func__);
		return (0); /* failure */
	}

	if (reply == NULL) {
		ERROR("%s: Invalid <reply> message to send.", __func__);
		return (0); /* failure */
	}

	if (rpc->msgid == 0) {
		/* parse and store message-id */
		retval = nc_msg_parse_msgid(rpc);
	} else {
		retval = rpc->msgid;
	}

	msg = nc_msg_dup ((struct nc_msg*) reply);

	if (rpc != NULL) {
		/* set message id */
		msg->msgid = strdup(retval);
		if (xmlStrcmp(msg->doc->children->name, BAD_CAST "rpc-reply") == 0) {
			xmlSetProp(msg->doc->children, BAD_CAST "message-id", BAD_CAST msg->msgid);
		}
	} else {
		/* unknown message ID, send reply without it */
		if (xmlStrcmp(msg->doc->children->name, BAD_CAST "rpc-reply") == 0) {
			xmlRemoveProp(xmlHasProp(msg->doc->children, BAD_CAST "message-id"));
		}
	}

	/* set proper namespace according to NETCONF version */
	xmlNewNs (msg->doc->children, BAD_CAST NC_NS_BASE10, NULL);

	/* send message */
	ret = nc_session_send (session, msg);

	nc_msg_free (msg);

	if (ret != EXIT_SUCCESS) {
		return (0);
	} else {
		if (reply->type.reply == NC_REPLY_ERROR) {
			/* update stats */
			session->stats.out_rpc_errors++;
			if (nc_stats) {nc_stats->counters.out_rpc_errors++;}
		}
		return (retval);
	}
}

int nc_msgid_compare (const nc_msgid id1, const nc_msgid id2)
{
	if (id1 == NULL || id2 == NULL) {
		return (-1);
	} else {
		return (strcmp(id1, id2));
	}
}

NC_MSG_TYPE nc_session_send_recv (struct nc_session* session, nc_rpc *rpc, nc_reply** reply)
{
	nc_msgid msgid1, msgid2;
	NC_MSG_TYPE replytype;
	struct nc_msg* queue = NULL, *msg, *p = NULL;

	msgid1 = nc_session_send_rpc(session, rpc);
	if (msgid1 == NULL) {
		return (NC_MSG_UNKNOWN);
	}

	DBG_LOCK("mut_mqueue");
	pthread_mutex_lock(&(session->mut_mqueue));

	/* first, look into the session's list of previously received messages */
	if ((queue = session->queue_msg) != NULL) {
		/* search in the queue for the reply with required message ID */
		for (msg = queue; msg != NULL; msg = msg->next) {
			/* test message IDs */
			if (nc_msgid_compare(msgid1, nc_reply_get_msgid((nc_reply*) msg)) == 0) {
				break;
			}

			/* store the predecessor */
			p = msg;
		}

		if (msg != NULL) {
			/* we have found the reply in the queue */
			(*reply) = (nc_reply*) msg;

			/* detach the reply from session's queue */
			if (p == NULL) {
				/* no predecessor - we detach the first item in the queue */
				session->queue_msg = msg->next;
			} else {
				p->next = msg->next;
			}
			msg->next = NULL;
			DBG_UNLOCK("mut_mqueue");
			pthread_mutex_unlock(&(session->mut_mqueue));
			return (NC_MSG_REPLY);
		} else {
			/*
			 * the queue does not contain required reply, we have to
			 * read it from the input, but first we have to hide
			 * session's queue from nc_session_recv_reply()
			 */
			session->queue_msg = NULL;
		}
	}
	DBG_UNLOCK("mut_mqueue");
	pthread_mutex_unlock(&(session->mut_mqueue));

	while (1) {
		replytype = nc_session_recv_reply(session, -1, reply);
		if (replytype == NC_MSG_REPLY) {
			/* compare message ID */
			if (nc_msgid_compare(msgid1, msgid2 = nc_reply_get_msgid(*reply)) != 0) {
				/* reply with different message ID is expected */
				DBG_LOCK("mut_mqueue");
				pthread_mutex_lock(&(session->mut_mqueue));
				/* store this reply for the later use of someone else */
				if (queue == NULL) {
					queue = (struct nc_msg*) (*reply);
				} else {
					for (msg = queue; msg->next != NULL; msg = msg->next);
					msg->next = (struct nc_msg*) (*reply);
				}
				DBG_UNLOCK("mut_mqueue");
				pthread_mutex_unlock(&(session->mut_mqueue));
			} else {
				/* we have it! */
				break;
			}
		} else if (replytype == NC_MSG_UNKNOWN || replytype == NC_MSG_NONE) {
			/* some error occured */
			break;
		}
		/*
		 * else (e.g. Notification or not expected reply was received)
		 * read another message in the loop
		 */
	}

	if (queue != NULL) {
		DBG_LOCK("mut_mqueue");
		pthread_mutex_lock(&(session->mut_mqueue));
		/* reconnect hidden queue back to the session */
		session->queue_msg = queue;
		DBG_UNLOCK("mut_mqueue");
		pthread_mutex_unlock(&(session->mut_mqueue));
	}

	return (replytype);
}

const char* nc_session_term_string(NC_SESSION_TERM_REASON reason)
{
	switch(reason) {
	case NC_SESSION_TERM_CLOSED:
		return ("closed");
		break;
	case NC_SESSION_TERM_KILLED:
		return ("killed");
		break;
	case NC_SESSION_TERM_DROPPED:
		return ("dropped");
		break;
	case NC_SESSION_TERM_TIMEOUT:
		return ("timeout");
		break;
	case NC_SESSION_TERM_BADHELLO:
		return ("bad-hello");
		break;
	default:
		return ("other");
		break;
	}
}
