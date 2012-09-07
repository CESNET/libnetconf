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

struct nc_session* nc_session_dummy(const char* sid, const char* username, struct nc_cpblts *capabilities)
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

	/*
	 * mut_in and mut_out fields are not initialized since dummy session
	 * can not send or receive any data
	 */

	/* session is DUMMY */
	session->status = NC_SESSION_STATUS_DUMMY;
	/* copy session id */
	strncpy (session->session_id, sid, SID_SIZE);
	/* copy user name */
	session->username = strdup (username);
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

void nc_session_close(struct nc_session* session, const char* msg)
{
	nc_rpc *rpc_close = NULL;
	nc_reply *reply = NULL;
	NC_SESSION_STATUS sstatus = session->status;

	/* lock session due to accessing its status and other items */
	if (sstatus != NC_SESSION_STATUS_DUMMY) {
		pthread_mutex_lock(&(session->mut_session));
	}

	/* close the SSH session */
	if (session != NULL && session->status != NC_SESSION_STATUS_CLOSING && session->status != NC_SESSION_STATUS_CLOSED) {
		if (session->ssh_channel != NULL) {
			if (session->status == NC_SESSION_STATUS_WORKING && libssh2_channel_eof(session->ssh_channel) == 0) {
				/* prevent infinite recursion when socket is corrupted -> stack overflow */
				session->status = NC_SESSION_STATUS_CLOSING;

				/* close NETCONF session */
				rpc_close = nc_rpc_closesession();
				if (rpc_close != NULL) {
					if (nc_session_send_rpc(session, rpc_close) != 0) {
						nc_session_recv_reply(session, &reply);
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
			libssh2_session_disconnect(session->ssh_session, msg);
			libssh2_session_free(session->ssh_session);
			session->ssh_session = NULL;
		}
		if (session->hostname != NULL) {
			free(session->hostname);
			session->hostname = NULL;
		}
		if (session->port != NULL) {
			free(session->port);
			session->port = NULL;
		}
		if (session->libssh2_socket != -1) {
			close(session->libssh2_socket);
			session->libssh2_socket = -1;
		}

		if (sstatus != NC_SESSION_STATUS_DUMMY) {
			session->status = NC_SESSION_STATUS_CLOSED;
			pthread_mutex_unlock(&(session->mut_session));
			/* destroy mutexes */
			pthread_mutex_destroy(&(session->mut_in));
			pthread_mutex_destroy(&(session->mut_out));
			pthread_mutex_destroy(&(session->mut_session));
		} else {
			session->status = NC_SESSION_STATUS_CLOSED;

		}

		/*
		 * username, capabilities and session_id are untouched
		 */

		/* successfully closed */
	}
}

void nc_session_free (struct nc_session* session)
{
	nc_session_close(session, "Final closing of the NETCONF session.");

	/* free items untouched by nc_session_close() */
	if (session->username != NULL) {
		free (session->username);
	}
	if (session->capabilities != NULL) {
		nc_cpblts_free(session->capabilities);
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
	if (session->status != NC_SESSION_STATUS_WORKING) {
		return (EXIT_FAILURE);
	}


	xmlDocDumpFormatMemory (msg->doc, (xmlChar**) (&text), &len, 1);
	DBG("Writing message: %s", text);

	/* lock the session for sending the data */
	pthread_mutex_lock(&(session->mut_out));

	/* if v1.1 send chunk information before message */
	if (session->version == NETCONFV11) {
		snprintf (buf, 1024, "\n#%d\n", (int) strlen (text));
		c = 0;
		do {
			NC_WRITE(session, &(buf[c]), c);
			if (c == LIBSSH2_ERROR_TIMEOUT) {
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
			pthread_mutex_unlock(&(session->mut_out));
			VERB("Writing data into the communication channel timeouted.");
			return (EXIT_FAILURE);
		}
	} while (c != strlen (text));

	/* unlock the session's output */
	pthread_mutex_unlock(&(session->mut_out));

	return (EXIT_SUCCESS);
}

int nc_session_read_len (struct nc_session* session, size_t chunk_length, char **text, size_t *len)
{
	char *buf, *err_msg;
	ssize_t c;
	size_t rd = 0;

	/* check if we can work with the session */
	if (session->status != NC_SESSION_STATUS_WORKING) {
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
			if (c == LIBSSH2_ERROR_EAGAIN) {
				usleep (NC_READ_SLEEP);
				continue;
			} else if (c < 0) {
				libssh2_session_last_error (session->ssh_session, &err_msg, NULL, 0);
				ERROR("Reading from SSH channel failed (%s)", err_msg);
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
	if (session->status != NC_SESSION_STATUS_WORKING) {
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
			if (c == LIBSSH2_ERROR_EAGAIN) {
				usleep (NC_READ_SLEEP);
				continue;
			} else if (c < 0) {
				libssh2_session_last_error (session->ssh_session, &err_msg, NULL, 0);
				ERROR("Reading from SSH channel failed (%s)", err_msg);
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

	/* parse and store message-id */
	ret = (char*) xmlGetProp(msg->doc->children, BAD_CAST "message-id");
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

NC_MSG_TYPE nc_session_receive (struct nc_session* session, struct nc_msg** msg)
{
	struct nc_msg *retval;
	nc_reply* reply;
	const char* id;
	char *text = NULL, *chunk = NULL;
	size_t len;
	unsigned long long int text_size = 0, total_len = 0;
	size_t chunk_length;
	struct pollfd fds;
	int status;
	NC_MSG_TYPE msgtype;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to receive data.");
		return (NC_MSG_UNKNOWN);
	}

	/* lock the session for receiving */
	pthread_mutex_lock(&(session->mut_in));

	/* use while for possibility of repeating test */
	while (session->ssh_channel == NULL && session->fd_input != -1) {
		/* check input state (only for file descriptor, not libssh2) via poll */
		fds.fd = session->fd_input;
		fds.events = POLLIN;
		fds.revents = 0;
		status = poll(&fds, 1, 100);
		if (status == 0 ||  (status == -1 && errno == EINTR)) {
			/* poll timed out or was interrupted */
			continue;
		} else if (status < 0) {
			/* poll failed - something wrong happend, close this socket and wait for another request */
			ERROR("Input channel error");
			nc_session_close(session, "Input stream closed");
			pthread_mutex_unlock(&(session->mut_in));
			return (NC_MSG_UNKNOWN);

		}
		/* status > 0 */
		/* check the status of the socket */
		/* if nothing to read and POLLHUP (EOF) or POLLERR set */
		if ((fds.revents & POLLHUP) || (fds.revents & POLLERR)) {
			/* close client's socket (it's probably already closed by client */
			ERROR("Input channel closed");
			nc_session_close(session, "Input stream closed");
			pthread_mutex_unlock(&(session->mut_in));
			return (NC_MSG_UNKNOWN);
		}
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

	pthread_mutex_unlock(&(session->mut_in));

	retval = malloc (sizeof(struct nc_msg));
	if (retval == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		free (text);
		goto malformed_msg;
	}
	retval->error = NULL;

	/* store the received message in libxml2 format */
	retval->doc = xmlReadDoc (BAD_CAST text, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (retval->doc == NULL) {
		free (retval);
		free (text);
		ERROR("Invalid XML data received.");
		goto malformed_msg;
	}
	free (text);

	/* parse and store message-id */
	if ((id = nc_msg_parse_msgid(retval)) == NULL) {
		retval->msgid = NULL;
	} else {
		retval->msgid = strdup(id);
	}

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
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "get-config") == 0)) {
			retval->type.rpc = NC_RPC_DATASTORE_READ;
		} else if ((xmlStrcmp (retval->doc->children->children->name, BAD_CAST "copy-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "delete-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "edit-config") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "lock") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "unlock") == 0)) {
			retval->type.rpc = NC_RPC_DATASTORE_WRITE;
		} else if ((xmlStrcmp (retval->doc->children->children->name, BAD_CAST "kill-session") == 0) ||
				(xmlStrcmp (retval->doc->children->children->name, BAD_CAST "close-session") == 0)){
			retval->type.rpc = NC_RPC_SESSION;
		} else {
			retval->type.rpc = NC_RPC_UNKNOWN;
		}
	} else if (xmlStrcmp (retval->doc->children->name, BAD_CAST "hello") == 0) {
		/* set message type, we have <hello> message */
		retval->type.reply = NC_REPLY_HELLO;
		msgtype = NC_MSG_HELLO;
	} else {
		WARN("Unknown (unsupported) type of received message detected.");
		retval->type.rpc = NC_RPC_UNKNOWN;
		msgtype = NC_MSG_UNKNOWN;
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
			nc_session_close(session, "Malformed NETCONF message received.");
			return (NC_MSG_UNKNOWN);
		}

		nc_session_send_reply(session, NULL, reply);
		nc_reply_free(reply);
	}

	ERROR("Malformed message received, closing the session %s.", session->session_id);
	nc_session_close(session, "Malformed NETCONF message received.");

	return (NC_MSG_UNKNOWN);
}

NC_MSG_TYPE nc_session_recv_reply (struct nc_session* session, nc_reply** reply)
{
	NC_MSG_TYPE ret;

	ret = nc_session_receive (session, (struct nc_msg**) reply);
	if (ret != NC_MSG_REPLY) {
		return (NC_MSG_UNKNOWN);
	} else {
		if (nc_reply_get_type (*reply) == NC_REPLY_ERROR &&
				callbacks.process_error_reply != NULL) {
			/* process rpc-error reply */
			callbacks.process_error_reply((*reply)->error->tag,
					(*reply)->error->type,
					(*reply)->error->severity,
					(*reply)->error->apptag,
					(*reply)->error->path,
					(*reply)->error->message,
					(*reply)->error->attribute,
					(*reply)->error->element,
					(*reply)->error->ns,
					(*reply)->error->sid);
			/* free the data */
			nc_reply_free(*reply);
			*reply = NULL;
			return (NC_MSG_NONE);
		}
		return (NC_MSG_REPLY);
	}
}

NC_MSG_TYPE nc_session_recv_rpc (struct nc_session* session, nc_rpc** rpc)
{
	NC_MSG_TYPE ret;
	const char* wd;
	struct nc_err* e = NULL;
	nc_reply* reply;

	ret = nc_session_receive (session, (struct nc_msg**) rpc);
	if (ret != NC_MSG_RPC) {
		return (NC_MSG_UNKNOWN);
	} else {
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
				return (0); /* failure */
			}
		}

		return (NC_MSG_RPC);
	}
}

const nc_msgid nc_session_send_rpc (struct nc_session* session, nc_rpc *rpc)
{
	int ret;
	char msg_id_str[16];
	const char* wd;
	struct nc_msg *msg;

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING && session->status != NC_SESSION_STATUS_CLOSING)) {
		ERROR("Invalid session to send <rpc>.");
		return (NULL); /* failure */
	}

	if (rpc->type.rpc != NC_RPC_HELLO) {
		/* check for capabilities operations */
		/* :notifications */
		if (nc_rpc_get_op(rpc) == NC_OP_CREATESUBSCRIPTION) {
			if (nc_cpblts_enabled(session, NC_CAP_NOTIFICATION_ID) == 0) {
				ERROR("RPC requires :notifications capability, but session does not support it.");
				return (NULL); /* failure */
			}
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
DBG("msgid: %llu", session->msgid);
	/* set message id */
	if (xmlStrcmp (msg->doc->children->name, BAD_CAST "rpc") == 0) {
		/* lock the session due to accessing msgid item */
		pthread_mutex_lock(&(session->mut_session));
		sprintf (msg_id_str, "%llu", session->msgid++);
		pthread_mutex_unlock(&(session->mut_session));
		xmlSetProp (msg->doc->children, BAD_CAST "message-id", BAD_CAST msg_id_str);
	} else {
		/* hello message */
		sprintf (msg_id_str, "hello");
	}
DBG("msgid: %llu (%s)", session->msgid, msg_id_str);

	/* set proper namespace according to NETCONF version */
	xmlNewNs (msg->doc->children, (xmlChar *) (
	        (session->version == NETCONFV10) ? NC_NS_BASE10 : NC_NS_BASE11), NULL);

	/* send message */
	ret = nc_session_send (session, msg);

	nc_msg_free (msg);

	if (ret != EXIT_SUCCESS) {
		if (rpc->type.rpc != NC_RPC_HELLO) {
			pthread_mutex_lock(&(session->mut_session));
			session->msgid--;
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

	if (session == NULL || (session->status != NC_SESSION_STATUS_WORKING)) {
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
	xmlNewNs (msg->doc->children, (xmlChar *) (
	        (session->version == NETCONFV10) ? NC_NS_BASE10 : NC_NS_BASE11), NULL);

	/* send message */
	ret = nc_session_send (session, msg);

	nc_msg_free (msg);

	if (ret != EXIT_SUCCESS) {
		return (0);
	} else {
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

nc_reply *nc_session_send_recv (struct nc_session* session, nc_rpc *rpc)
{
	nc_msgid msgid1, msgid2;
	nc_reply *reply = NULL;
	NC_MSG_TYPE replytype;

	msgid1 = nc_session_send_rpc(session, rpc);
	if (msgid1 == NULL) {
		return (NULL);
	}

	/* \todo: handling asynchronous messages (Notifications) using message queues */
	replytype = nc_session_recv_reply(session, &reply);
	if (replytype == NC_MSG_REPLY) {
		/* compare message ID */
		if (nc_msgid_compare(msgid1, msgid2 = nc_reply_get_msgid(reply)) != 0) {
			WARN("Sent message \'%s\', but received message is \'%s\'.", msgid1, msgid2);
		}
	} else {
		return (NULL);
	}

	return (reply);
}
