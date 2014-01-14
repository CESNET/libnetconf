/**
 * \file ssh.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to connect to NETCONF server via SSH2.
 *
 * Copyright (c) 2012-2014 CESNET, z.s.p.o.
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

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#ifdef HAVE_UTMPX_H
#	include <utmpx.h>
#endif

#ifdef DISABLE_LIBSSH
#	include <ctype.h>
#	include <pty.h>
#	include <termios.h>
#	include <libxml/xpath.h>
#	include <libxml/xpathInternals.h>
#else
#	include "libssh2.h"
#endif
#ifndef DISABLE_URL
#	include "url_internal.h"
#endif

#include "ssh.h"
#include "callbacks.h"
#include "callbacks_ssh.h"
#include "messages.h"
#include "session.h"
#include "netconf_internal.h"
#include "messages_internal.h"
#include "with_defaults.h"

#ifndef DISABLE_NOTIFICATIONS
#  include "notifications.h"
#endif

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

#define BUFFER_SIZE 4096
#define SSH2_TIMEOUT 10000 /* timeout for blocking functions in miliseconds */

extern struct nc_shared_info *nc_info;
extern char* server_capabilities; /* from datastore, only for server side */

#ifndef DISABLE_URL
	extern int nc_url_protocols;
#endif
	
struct auth_pref_couple
{
	NC_SSH_AUTH_TYPE type;
	short int value;
};

#define AUTH_COUNT 3
static struct auth_pref_couple sshauth_pref[AUTH_COUNT] = {
		{ NC_SSH_AUTH_INTERACTIVE, 3 },
		{ NC_SSH_AUTH_PASSWORD, 2 },
		{ NC_SSH_AUTH_PUBLIC_KEYS, 1 }
};

/* number of characters to store short number */
#define SHORT_INT_LENGTH 6

/* definition in session.c */
void parse_wdcap(struct nc_cpblts *capabilities, NCWD_MODE *basic, int *supported);
/* definition in datastore.c */
char **get_schemas_capabilities(void);

void nc_ssh_pref(NC_SSH_AUTH_TYPE type, short int preference)
{
	int dir = 0;
	unsigned short i;
	struct auth_pref_couple new, aux;

	new.type = type;
	new.value = preference;

	for (i = 0; i < AUTH_COUNT; i++) {
		if (sshauth_pref[i].type == new.type) {
			if (sshauth_pref[i].value < new.value) {
				sshauth_pref[i] = new;
				dir = -1;
				/* correct order */
				while ((i + dir) >= 0) {
					if (sshauth_pref[i].value >= sshauth_pref[i + dir].value) {
						aux = sshauth_pref[i + dir];
						sshauth_pref[i + dir] = sshauth_pref[i];
						sshauth_pref[i] = aux;
						i += dir;
					} else {
						break; /* WHILE */
					}
				}
			} else if (sshauth_pref[i].value > new.value) {
				sshauth_pref[i] = new;
				dir = 1;
				/* correct order */
				while ((i + dir) < AUTH_COUNT) {
					if (sshauth_pref[i].value < sshauth_pref[i + dir].value) {
						aux = sshauth_pref[i + dir];
						sshauth_pref[i + dir] = sshauth_pref[i];
						sshauth_pref[i] = aux;
						i += dir;
					} else {
						break; /* WHILE */
					}
				}
			}
			break; /* FOR */
		}
	}
}

static char** nc_accept_server_cpblts(char ** server_cpblts_list, char ** client_cpblts_list, int *version)
{
	int i, j, c;
	char **result = NULL;

	if (server_cpblts_list == NULL || client_cpblts_list == NULL) {
		ERROR("%s: Invalid parameters.", __func__);
		return (NULL);
	}

	if (version != NULL) {
		(*version) = NETCONFVUNK;
	}

	/* count max size of the resulting list */
	for (c = 0; server_cpblts_list[c] != NULL; c++);

	if ((result = malloc((c + 1) * sizeof(char*))) == NULL) {
		ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	c = 0;
	for (i = 0; server_cpblts_list[i] != NULL; i++) {
		if (strstr(server_cpblts_list[i], "urn:ietf:params:netconf:base:") == NULL) {
			result[c++] = strdup(server_cpblts_list[i]);
		} else {
			/* some of the base capability detected - check that both sides support it */
			for (j = 0; client_cpblts_list[j] != NULL; j++) {
				if (strcmp(server_cpblts_list[i], client_cpblts_list[j]) == 0) {
					result[c++] = strdup(server_cpblts_list[i]);
					break;
				}
			}
		}
	}
	result[c] = NULL;

	for (i = 0; result[i] != NULL; i++) { /* Try to find one of the netconf base capability */
		if (strcmp(NC_CAP_BASE11_ID, result[i]) == 0) {
			(*version) = NETCONFV11;
			break;
			/* v 1.1 is preferred */
		}
		if (strcmp(NC_CAP_BASE10_ID, result[i]) == 0) {
			(*version) = NETCONFV10;
			/* continue in searching for higher version */
		}
	}

	if ((*version) == NETCONFVUNK) {
		ERROR("No base capability found in the capabilities intersection.");
		free(result);
		return (NULL);
	}

	return (result);
}

static char** nc_parse_hello(struct nc_msg *msg, struct nc_session *session)
{
	xmlNodePtr node, capnode;
	char *cap = NULL, *str = NULL;
	char **capabilities = NULL;
	int c;

	if ((node = xmlDocGetRootElement(msg->doc)) == NULL) {
		ERROR("Parsing a <hello> message failed - the document is empty.");
		return (NULL);
	}

	if (xmlStrcmp(node->name, BAD_CAST "hello")) {
		ERROR("Parsing a <hello> message failed - received a non-<hello> message.");
		return (NULL);
	}

	for (node = node->children; node != NULL; node = node->next) {
		if (xmlStrcmp(node->name, BAD_CAST "capabilities") == 0) {
			/* <capabilities> node */

			/* count capabilities */
			for (capnode = node->children, c = 0; capnode != NULL;
			                capnode = capnode->next) {
				c++;
			}

			/* allocate memory for the capability list */
			if ((capabilities = malloc((c + 1) * sizeof(char*))) == NULL) {
				ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
				return (NULL);
			}

			/* fill up capabilities list */
			for (capnode = node->children, c = 0; capnode != NULL;
			                capnode = capnode->next) {
				str = (char*) xmlNodeGetContent(capnode);
				if ((str == NULL) || (cap = nc_clrwspace(str)) == NULL) {
					ERROR("Parsing a <hello> message failed - unable to read the capabilities.");
					return (NULL);
				}
				xmlFree(BAD_CAST str);
				if (strnonempty(cap)) {
					capabilities[c++] = cap;
				}
			}
			/* list termination NULL */
			capabilities[c] = NULL;
		} else if (xmlStrcmp(node->name, BAD_CAST "session-id") == 0) {
			/* session-id - available only if the caller of this parsing is a client */
			if (session->session_id[0] == '\0') {
				str = (char*) xmlNodeGetContent(node);
				if (strlen(str) >= (size_t) SID_SIZE) {
					/* Session ID is too long and we cannot store it */
					ERROR("Received <session-id> is too long - terminating the session.");
					return (NULL);
				}
				strncpy(session->session_id, str, SID_SIZE - 1);
				xmlFree(BAD_CAST str);
			} else {
				/* as defined by RFC, we have to terminate the session */
				ERROR("Received <hello> message with <session-id> - terminating the session.");
				return (NULL);
			}
		} else {
			/* something unknown - log it now, maybe in the future we will be more strict and will be returning error */
			WARN("Unknown content of the <hello> message (%s), ignoring and trying to continue.", (char*) node->name);
		}
	}

	if (capabilities == NULL || capabilities[0] == NULL) {
		/* no capability received */
		ERROR("Parsing a <hello> message failed - no capabilities detected.");
		return (NULL);
	}

	/* everything OK, return the received list of supported capabilities */
	return (capabilities);
}

#ifdef DISABLE_LIBSSH
struct nc_msg* read_hello(struct nc_session *session)
{
	struct nc_msg *retval;
	nc_reply* reply;
	xmlNodePtr root;
	unsigned int i, size = BUFFER_SIZE;
	char *buffer = NULL, c, *aux_buffer;

	if (!(buffer = (char *) malloc(size * sizeof (char)))) {
		return NULL;
	}
	memset(buffer, '\0', size * sizeof (char));

	/* initial reading */
	while(isspace(buffer[0] = (char) fgetc(session->f_input)));
	for (i = 1; i < strlen(NC_V10_END_MSG); i++) {
		if ((!feof(session->f_input)) && (!ferror(session->f_input))) {
			buffer[i] = (char) fgetc(session->f_input);
		} else {
			free(buffer);
			return NULL;
		}
	}

	/*
	 * read next character and check ending character sequence for
	 * NC_V10_END_MSG
	 */
	if (strcmp(NC_V10_END_MSG, &buffer[i - (unsigned int)strlen(NC_V10_END_MSG)])) {
		while ((!feof(session->f_input)) && (!ferror(session->f_input)) && ((c = (char) fgetc(session->f_input)) != EOF)) {
			if (i == size - 1) { /* buffer is too small */
				/* allocate larger buffer */
				size = 2 * size;
				if (!(aux_buffer = (char *) realloc(buffer, size))) {
					free(buffer); /* free buffer that was too small and reallocation failed */
					return NULL;
				}
				buffer = aux_buffer;
			}
			/* store read character */
			buffer[i] = c;
			i++;

			/* check if the ending character sequence was read */
			if (!(strncmp(NC_V10_END_MSG, &buffer[i - (unsigned int)strlen(NC_V10_END_MSG)], strlen(NC_V10_END_MSG)))) {
				buffer[i - strlen(NC_V10_END_MSG)] = '\0';
				break;
			}
		}
	} else {
		/* message is empty and contains only END-MESSAGE marker */
		/* one option is to return NULL as error:
		 free(buffer);
		 return NULL;
		 * or return empty string (buffer): */
		buffer[0] = '\0';
	}
	fclose(session->f_input);
	session->f_input = NULL;

	/* create the message structure */
	retval = calloc (1, sizeof(struct nc_msg));
	if (retval == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		free (buffer);
		goto malformed_msg;
	}

	/* store the received message in libxml2 format */
	retval->doc = xmlReadDoc (BAD_CAST buffer, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (retval->doc == NULL) {
		free (retval);
		free (buffer);
		ERROR("Invalid XML data received.");
		goto malformed_msg;
	}
	free (buffer);

	/* create xpath evaluation context */
	if ((retval->ctxt = xmlXPathNewContext(retval->doc)) == NULL) {
		ERROR("%s: rpc message XPath context cannot be created.", __func__);
		nc_msg_free(retval);
		goto malformed_msg;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(retval->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(retval);
		goto malformed_msg;
	}

	/* parse and store message type */
	root = xmlDocGetRootElement(retval->doc);
	if (xmlStrcmp (root->name, BAD_CAST "hello") != 0) {
		ERROR("Unexpected (non-hello) message received.");
		nc_msg_free(retval);
		goto malformed_msg;
	} else {
		/* set message type, we have <hello> message */
		retval->type.reply = NC_REPLY_HELLO;
		retval->msgid = NULL;
	}

	return (retval);

malformed_msg:

	if (session->version == NETCONFV11 && session->ssh_session == NULL) {
		/* NETCONF version 1.1 define sending error reply from the server */
		reply = nc_reply_error(nc_err_new(NC_ERR_MALFORMED_MSG));
		if (reply == NULL) {
			ERROR("Unable to create a \'Malformed message\' reply");
			nc_session_close(session, NC_SESSION_TERM_OTHER);
			return (NULL);
		}

		nc_session_send_reply(session, NULL, reply);
		nc_reply_free(reply);
	}

	ERROR("Malformed message received, closing the session %s.", session->session_id);
	nc_session_close(session, NC_SESSION_TERM_OTHER);

	return (NULL);
}
#endif /* DISABLE_LIBSSH */

#define HANDSHAKE_SIDE_SERVER 1
#define HANDSHAKE_SIDE_CLIENT 2
static int nc_handshake(struct nc_session *session, char** cpblts, nc_rpc *hello, int side)
{
	int retval = EXIT_SUCCESS;
	int i;
	nc_reply *recv_hello = NULL;
	char **recv_cpblts = NULL, **merged_cpblts = NULL;

	if (nc_session_send_rpc(session, hello) == 0) {
		return (EXIT_FAILURE);
	}

#ifdef DISABLE_LIBSSH
	if (side == HANDSHAKE_SIDE_CLIENT) {
		recv_hello = read_hello(session);
	} else {
		nc_session_recv_reply(session, -1, &recv_hello);
	}
#else
	nc_session_recv_reply(session, -1, &recv_hello);
#endif
	if (recv_hello == NULL) {
		return (EXIT_FAILURE);
	}

	if ((recv_cpblts = nc_parse_hello((struct nc_msg*) recv_hello, session)) == NULL) {
		nc_reply_free(recv_hello);
		return (EXIT_FAILURE);
	}
	nc_reply_free(recv_hello);

	if (side == HANDSHAKE_SIDE_CLIENT) {
		merged_cpblts = nc_accept_server_cpblts(recv_cpblts, cpblts, &(session->version));
	} else if (side == HANDSHAKE_SIDE_SERVER) {
		merged_cpblts = nc_accept_server_cpblts(cpblts, recv_cpblts, &(session->version));
	}
	if (merged_cpblts == NULL) {
		retval = EXIT_FAILURE;
	} else if ((session->capabilities = nc_cpblts_new((const char* const*) merged_cpblts)) == NULL) {
		retval = EXIT_FAILURE;
	}

	if (recv_cpblts) {
		i = 0;
		while (recv_cpblts[i]) {
			free(recv_cpblts[i]);
			i++;
		}
		free(recv_cpblts);
	}

	if (merged_cpblts) {
		i = 0;
		while (merged_cpblts[i]) {
			free(merged_cpblts[i]);
			i++;
		}
		free(merged_cpblts);
	}

	return (retval);
}

static int nc_client_handshake(struct nc_session *session, char** cpblts)
{
	nc_rpc *hello;
	int retval;

	/* just to be sure, it should be already done */
	memset(session->session_id, '\0', SID_SIZE);

	/* create client's <hello> message */
	hello = nc_msg_client_hello(cpblts);
	if (hello == NULL) {
		return (EXIT_FAILURE);
	}

	retval = nc_handshake(session, cpblts, hello, HANDSHAKE_SIDE_CLIENT);
	nc_rpc_free(hello);

	return (retval);
}

static int nc_server_handshake(struct nc_session *session, char** cpblts)
{
	nc_rpc *hello;
	int pid;
	int retval;

	/* set session ID == PID */
	pid = (int)getpid();
	if (snprintf(session->session_id, SID_SIZE, "%d", pid) <= 0) {
		ERROR("Unable to generate the NETCONF session ID.");
		return (EXIT_FAILURE);
	}

	/* create server's <hello> message */
	hello = nc_msg_server_hello(cpblts, session->session_id);
	if (hello == NULL) {
		return (EXIT_FAILURE);
	}

	retval = nc_handshake(session, cpblts, hello, HANDSHAKE_SIDE_SERVER);
	nc_rpc_free(hello);

	if (retval != EXIT_SUCCESS) {
		if (nc_info) {
			pthread_rwlock_wrlock(&(nc_info->lock));
			nc_info->stats.bad_hellos++;
			pthread_rwlock_unlock(&(nc_info->lock));
		}
	}

	return (retval);
}

#ifndef DISABLE_LIBSSH
static int check_hostkey(const char *host, const char* knownhosts_dir, LIBSSH2_SESSION* ssh_session)
{
	int ret, knownhost_check, i;
	int fd;
	size_t len;
	const char *remotekey, *fingerprint_raw;
	char *knownhosts_file = NULL;
	int hostkey_type, hostkey_typebit;
	struct libssh2_knownhost *ssh_host;
	LIBSSH2_KNOWNHOSTS *knownhosts;

	/*
	 * to print MD5 raw hash, we need 3*16 + 1 bytes (4 characters are printed
	 * all the time, but except the last one, NULL terminating bytes are
	 * rewritten by the following value). In the end, the last ':' is removed
	 * for nicer output, so there are two terminating NULL bytes in the end.
	 */
	char fingerprint_md5[49];

	/* MD5 hash size is 16B, SHA1 hash size is 20B */
	fingerprint_raw = libssh2_hostkey_hash(ssh_session, LIBSSH2_HOSTKEY_HASH_MD5);
	for (i = 0; i < 16; i++) {
		sprintf(&fingerprint_md5[i * 3], "%02x:", (uint8_t) fingerprint_raw[i]);
	}
	fingerprint_md5[47] = 0;

	knownhosts = libssh2_knownhost_init(ssh_session);
	if (knownhosts == NULL) {
		ERROR("Unable to init the knownhost check.");
	} else {
		/* get host's fingerprint */
		remotekey = libssh2_session_hostkey(ssh_session, &len, &hostkey_type);
		if (remotekey == NULL && hostkey_type == LIBSSH2_HOSTKEY_TYPE_UNKNOWN) {
			ERROR("Unable to get host key.");
			libssh2_knownhost_free(knownhosts);
			return (EXIT_FAILURE);
		}
		hostkey_typebit = (hostkey_type == LIBSSH2_HOSTKEY_TYPE_RSA) ? LIBSSH2_KNOWNHOST_KEY_SSHRSA : LIBSSH2_KNOWNHOST_KEY_SSHDSS;

		if (knownhosts_dir == NULL) {
			/* we are not able to use knownhosts file */
			ret = -1;
		} else {
			/* set general knownhosts file used also by OpenSSH's applications */
			if (asprintf(&knownhosts_file, "%s/known_hosts", knownhosts_dir) == -1) {
				ERROR("%s: asprintf() failed.", __func__);
				libssh2_knownhost_free(knownhosts);
				return(EXIT_FAILURE);
			}

			/* get all the hosts */
			ret = libssh2_knownhost_readfile(knownhosts, knownhosts_file, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			if (ret < 0) {
				/*
				 * default known_hosts may contain keys that are not supported
				 * by libssh2, so try to use libnetconf's specific known_hosts
				 * file located in the same directory and named
				 * 'netconf_known_hosts'
				 */
				free(knownhosts_file);
				knownhosts_file = NULL;
				if (asprintf(&knownhosts_file, "%s/netconf_known_hosts", knownhosts_dir) == -1) {
					ERROR("%s: asprintf() failed.", __func__);
					libssh2_knownhost_free(knownhosts);
					return(EXIT_FAILURE);
				}
				/* create own knownhosts file if it does not exist */
				if (eaccess(knownhosts_file, F_OK) != 0) {
					if ((fd = creat(knownhosts_file, S_IWUSR | S_IRUSR |S_IRGRP | S_IROTH)) != -1) {
						close(fd);
					}
				}
				ret = libssh2_knownhost_readfile(knownhosts, knownhosts_file, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			}
		}

		if (ret < 0) {
			WARN("Unable to check against the knownhost file (%s).", knownhosts_file);
			if (callbacks.hostkey_check(host, hostkey_type, fingerprint_md5) == 0) {
				/* host authenticity authorized */
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_SUCCESS);
			} else {
				VERB("Host authenticity check negative.");
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_FAILURE);
			}
		} else {
			knownhost_check = libssh2_knownhost_check(knownhosts,
					host,
					remotekey,
					len,
					LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | hostkey_typebit,
					&ssh_host);

			DBG("Host check: %d, key: %s\n", knownhost_check,
					(knownhost_check <= LIBSSH2_KNOWNHOST_CHECK_MATCH) ? ssh_host->key : "<none>");

			switch (knownhost_check) {
			case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
				ERROR("Remote host %s identification changed!", host);
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_FAILURE);
			case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
				ERROR("Knownhost checking failed.");
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_FAILURE);
			case LIBSSH2_KNOWNHOST_CHECK_MATCH:
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_SUCCESS);
			case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
				if (callbacks.hostkey_check(host, hostkey_type, fingerprint_md5) == 1) {
					VERB("Host authenticity check negative.");
					free(knownhosts_file);
					libssh2_knownhost_free(knownhosts);
					return (EXIT_FAILURE);
				}
				/* authenticity authorized */
				break;
			}

			ret = libssh2_knownhost_add(knownhosts,
					host,
					NULL,
					remotekey,
					len,
					LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | hostkey_typebit,
					NULL);
			if (ret != 0) {
				WARN("Adding the known host %s failed!", host);
			} else if (knownhosts_file != NULL) {
				ret = libssh2_knownhost_writefile(knownhosts,
						knownhosts_file,
						LIBSSH2_KNOWNHOST_FILE_OPENSSH);
				if (ret) {
					WARN("Writing %s failed!", knownhosts_file);
				}
			} else {
				WARN("Unknown known_hosts file location, skipping the writing of your decision.");
			}

			libssh2_knownhost_free(knownhosts);
			free(knownhosts_file);
			return (EXIT_SUCCESS);
		}

	}

	return (EXIT_FAILURE);
}
#endif /* not DISABLE_LIBSSH */

static char* serialize_cpblts(const struct nc_cpblts *capabilities)
{
	char *aux = NULL, *retval = NULL;
	int i;

	if (capabilities == NULL) {
		return (NULL);
	}

	for (i = 0; i < capabilities->items; i++) {
		if (asprintf(&retval, "%s<capability>%s</capability>",
				(aux == NULL) ? "" : aux,
				capabilities->list[i]) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			continue;
		}
		free(aux);
		aux = retval;
		retval = NULL;
	}
	if (asprintf(&retval, "<capabilities>%s</capabilities>", aux) == -1) {
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		retval = NULL;
	}
	free(aux);
	return(retval);
}

struct nc_session *nc_session_accept(const struct nc_cpblts* capabilities)
{
	int r, i;
	struct nc_session *retval = NULL;
	struct nc_cpblts *server_cpblts = NULL;
	struct passwd *pw;
	char *wdc, *wdc_aux, *straux;
	char list[255];
	NCWD_MODE mode;
	char** nslist;
	pthread_mutexattr_t mattr;
#ifdef HAVE_UTMPX_H
	struct utmpx protox, *utp;
#endif

	/* allocate netconf session structure */
	retval = malloc(sizeof(struct nc_session));
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		return (NULL);
	}
	memset(retval, 0, sizeof(struct nc_session));
	if ((retval->stats = malloc (sizeof (struct nc_session_stats))) == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		free(retval);
		return NULL;
	}
	retval->is_server = 1;
	retval->libssh2_socket = -1;
	retval->fd_input = STDIN_FILENO;
	retval->fd_output = STDOUT_FILENO;
	retval->msgid = 1;
	retval->queue_event = NULL;
	retval->queue_msg = NULL;
	retval->monitored = 0;
	retval->stats->in_rpcs = 0;
	retval->stats->in_bad_rpcs = 0;
	retval->stats->out_rpc_errors = 0;
	retval->stats->out_notifications = 0;
	
	if (pthread_mutexattr_init(&mattr) != 0) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	retval->mut_libssh2_channels = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	if ((r = pthread_mutex_init(retval->mut_libssh2_channels, &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_mqueue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_equeue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_session), &mattr)) != 0) {
		ERROR("Mutex initialization failed (%s).", strerror(r));
		pthread_mutexattr_destroy(&mattr);
		return (NULL);
	}
	pthread_mutexattr_destroy(&mattr);

	/*
	 * get username - we are running as SSH Subsystem which was started
	 * under the user which was connecting to NETCONF server
	 */
	pw = getpwuid(getuid());
	if (pw == NULL) {
		/* unable to get correct username */
		ERROR("Unable to set an username for the SSH connection (%s).", strerror(errno));
		nc_session_close(retval, NC_SESSION_TERM_OTHER);
		return (NULL);
	}
	retval->username = strdup(pw->pw_name);
	retval->groups = nc_get_grouplist(retval->username);
	/* detect if user ID is nacm_recovery_uid -> then the session is recovery */
	if (pw->pw_uid == NACM_RECOVERY_UID) {
		retval->nacm_recovery = 1;
	} else {
		retval->nacm_recovery = 0;
	}

	if (capabilities == NULL) {
		if ((server_cpblts = nc_session_get_cpblts_default()) == NULL) {
			VERB("Unable to set the client's NETCONF capabilities.");
			nc_session_close(retval, NC_SESSION_TERM_OTHER);
			return (NULL);
		}
	} else {
		server_cpblts = nc_cpblts_new((const char* const*)(capabilities->list));
	}
	/* set with-defaults capability announcement */
	if ((nc_cpblts_get(server_cpblts, NC_CAP_WITHDEFAULTS_ID) != NULL)
         && ((mode = ncdflt_get_basic_mode()) != NCWD_MODE_NOTSET)) {
		switch(mode) {
		case NCWD_MODE_ALL:
			wdc_aux = "?basic-mode=report-all";
			break;
		case NCWD_MODE_TRIM:
			wdc_aux = "?basic-mode=trim";
			break;
		case NCWD_MODE_EXPLICIT:
			wdc_aux = "?basic-mode=explicit";
			break;
		default:
			wdc_aux = NULL;
			break;
		}
		if (wdc_aux != NULL) {
			mode = ncdflt_get_supported();
			list[0] = 0;
			if ((mode & NCWD_MODE_ALL) != 0) {
				strcat(list, ",report-all");
			}
			if ((mode & NCWD_MODE_ALL_TAGGED) != 0) {
				strcat(list, ",report-all-tagged");
			}
			if ((mode & NCWD_MODE_TRIM) != 0) {
				strcat(list, ",trim");
			}
			if ((mode & NCWD_MODE_EXPLICIT) != 0) {
				strcat(list, ",explicit");
			}

			if (strnonempty(list)) {
				list[0] = '='; /* replace initial comma */
				r = asprintf(&wdc, "urn:ietf:params:netconf:capability:with-defaults:1.0%s&amp;also-supported%s", wdc_aux, list);
			} else {
				/* no also-supported */
				r = asprintf(&wdc, "urn:ietf:params:netconf:capability:with-defaults:1.0%s", wdc_aux);
			}

			if (r != -1) {
				/* add/update capabilities list */
				nc_cpblts_add(server_cpblts, wdc);
				free(wdc);
			} else {
				WARN("asprintf() failed - with-defaults capability parameters may not be set properly (%s:%d).", __FILE__, __LINE__);
			}
		}
	}

#ifndef DISABLE_URL
	if (nc_cpblts_get(server_cpblts, NC_CAP_URL_ID) != NULL) {
		/* update URL capability with enabled protocols */
		straux = nc_url_gencap();
		nc_cpblts_add(server_cpblts, straux);
		free(straux);
	}
#endif

	if (server_capabilities != NULL) {
		free (server_capabilities);
		server_capabilities = serialize_cpblts(server_cpblts);
	}

	retval->status = NC_SESSION_STATUS_WORKING;

	/* add namespaces of used datastores as announced capabilities */
	if ((nslist = get_schemas_capabilities()) != NULL) {
		for(i = 0; nslist[i] != NULL; i++) {
			nc_cpblts_add(server_cpblts, nslist[i]);
			free(nslist[i]);
		}
		free(nslist);
	}

	if (nc_server_handshake(retval, server_cpblts->list) != 0) {
		nc_session_close(retval, NC_SESSION_TERM_BADHELLO);
		nc_session_free(retval);
		nc_cpblts_free(server_cpblts);
		return (NULL);
	}

	/* get client's hostname */
	/*
	 * This is not a critical information, so only warnings are generated if
	 * the following process fails. The hostname of the client is used by
	 * server to generate NETCONF base notifications.
	 */
	if ((straux = getenv("SSH_CLIENT")) != NULL) {
		/* OpenSSH implementation provides SSH_CLIENT environment variable */
		retval->hostname = strdup(straux);
		if ((straux = strchr(retval->hostname, ' ')) != NULL ) {
			*straux = 0; /* null byte after IP in $SSH_CLIENT */
		}
#ifdef HAVE_UTMPX_H
	} else {
		/*
		 * in other cases, we will try to get information from the utmpx
		 * file of this session
		 */
		if ((straux = ttyname(fileno(stdin))) == 0) {
			WARN("Unable to get tty (%s) to get the client's hostname (session %s).", strerror(errno), retval->session_id);
		} else {
			if (strncmp(straux, "/dev/", 5) == 0) {
				straux += 5;
			}
			memset(&protox, 0, sizeof protox);
			strcpy(protox.ut_line, straux);

			if ((utp = getutxline(&protox)) == 0) {
				WARN("Unable to locate UTMPX for \'%s\' to get the client's hostname (session %s).", straux, retval->session_id);
			} else {
				retval->hostname = malloc(sizeof(char) * (1 + sizeof(utp->ut_host)));
				memcpy(retval->hostname, utp->ut_host, sizeof(utp->ut_host));
				retval->hostname[sizeof(utp->ut_host)] = 0;
			}
		}
#endif
	}

	/* set with-defaults capability flags */
	parse_wdcap(retval->capabilities, &(retval->wd_basic), &(retval->wd_modes));

	/* cleanup */
	nc_cpblts_free(server_cpblts);

#ifndef DISABLE_NOTIFICATIONS
	/* log start of the session */
	ncntf_event_new(-1, NCNTF_BASE_SESSION_START, retval);
#endif

	retval->logintime = nc_time2datetime(time(NULL));
	if (nc_info) {
		pthread_rwlock_wrlock(&(nc_info->lock));
		nc_info->stats.sessions_in++;
		pthread_rwlock_unlock(&(nc_info->lock));
	}

	return (retval);
}

#ifndef DISABLE_LIBSSH
static int find_ssh_keys ()
{
	struct passwd *pw;
	char * user_home, *key_pub_path = NULL, *key_priv_path = NULL;
	char * key_names[SSH2_KEYS] = {"id_rsa", "id_dsa", "id_ecdsa"};
	int i, x, y, retval = EXIT_FAILURE;

	if ((pw = getpwuid(getuid())) == NULL) {
		ERROR("Determining user's home directory for getting SSH keys failed (%s)", strerror(errno));
		return EXIT_FAILURE;
	}
	user_home = pw->pw_dir;

	/* search in the same location as ssh do (~/.ssh/) */
	VERB ("Searching for the key pairs in the standard ssh directory.");
	for (i = 0; i < SSH2_KEYS; i++) {
		x = asprintf (&key_priv_path, "%s/.ssh/%s", user_home, key_names[i]);
		y = asprintf (&key_pub_path, "%s/.ssh/%s.pub", user_home, key_names[i]);
		if (x == -1 || y == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			continue;
		}
		if (eaccess(key_priv_path, R_OK) == 0 && eaccess(key_pub_path, R_OK) == 0) {
			VERB("Found a pair %s[.pub]", key_priv_path);
			nc_set_keypair_path(key_priv_path, key_pub_path);
			retval = EXIT_SUCCESS;
		}
		free (key_priv_path);
		free (key_pub_path);
	}

	return retval;
}
#endif /* not DISABLE_LIBSSH */

struct nc_session *nc_session_connect(const char *host, unsigned short port, const char *username, const struct nc_cpblts* cpblts)
{
	struct nc_session *retval = NULL;
	struct nc_cpblts *client_cpblts = NULL;
	pthread_mutexattr_t mattr;
	char port_s[SHORT_INT_LENGTH];
	struct passwd *pw;
	char *knownhosts_dir = NULL;
	char *s;
	int r;

	/* set default values */
	if (host == NULL || strisempty(host)) {
		host = "localhost";
	}
	if (port == 0) {
		port = NC_PORT;
	}

	if (snprintf(port_s, SHORT_INT_LENGTH, "%d", port) < 0) {
		/* converting short int to the string failed */
		ERROR("Unable to convert the port number to a string.");
		return (NULL);
	}

#ifdef DISABLE_LIBSSH

	pid_t sshpid; /* child's PID */
	struct termios termios;
	struct winsize win;
	int pout[2], ssh_in;
	int ssh_fd, count = 0;
	char buffer[BUFFER_SIZE];
	char tmpchar[2];
	char line[81];
	int forced = 0; /* force connection to unknown destinations */
	size_t n;
	gid_t newgid, oldgid;
	uid_t newuid, olduid;

	if (access(SSH_PROG, X_OK) != 0) {
		ERROR("Unable to locate or execute ssh(1) application \'%s\' (%s).", SSH_PROG, strerror(errno));
		return(NULL);
	}

	/* get current user if not specified */
	if (username == NULL) {
		pw = getpwuid(getuid());
		if (pw == NULL) {
			/* unable to get correct username (errno from getpwuid) */
			ERROR("Unable to set the username for the SSH connection (%s).", strerror(errno));
			return (NULL);
		} else {
			username = pw->pw_name;
		}
	}

	/* allocate netconf session structure */
	retval = malloc(sizeof(struct nc_session));
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		return (NULL);
	}
	memset(retval, 0, sizeof(struct nc_session));
	if ((retval->stats = malloc(sizeof(struct nc_session_stats))) == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		free(retval);
		return NULL;
	}
	retval->is_server = 0;
	retval->libssh2_socket = -1;
	retval->ssh_session = NULL;
	retval->hostname = strdup(host);
	retval->username = strdup(username);
	retval->groups = NULL; /* client side does not need this information */
	retval->port = strdup(port_s);
	retval->msgid = 1;
	retval->queue_event = NULL;
	retval->queue_msg = NULL;
	retval->logintime = NULL;
	retval->monitored = 0;
	retval->nacm_recovery = 0; /* not needed/decidable on the client side */
	retval->stats->in_rpcs = 0;
	retval->stats->in_bad_rpcs = 0;
	retval->stats->out_rpc_errors = 0;
	retval->stats->out_notifications = 0;

	if (pthread_mutexattr_init(&mattr) != 0) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	retval->mut_libssh2_channels = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	if ((r = pthread_mutex_init(retval->mut_libssh2_channels, &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_mqueue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_equeue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_session), &mattr)) != 0) {
		ERROR("Mutex initialization failed (%s).", strerror(r));
		pthread_mutexattr_destroy(&mattr);
		return (NULL);
	}
	pthread_mutexattr_destroy(&mattr);

	/* create communication pipes */
	if (pipe(pout) == -1) {
		ERROR("%s: Unable to create communication pipes", __func__);
		return (NULL);
	}
	retval->fd_output = pout[1];
	ssh_in = pout[0];

	/* Get current properties of tty */
	ioctl(0, TIOCGWINSZ, &win);
	if (tcgetattr(STDIN_FILENO, &termios) < 0) {
		ERROR("%s", strerror(errno));
		return (NULL);
	}

	/* create child process */
	if ((sshpid = forkpty(&ssh_fd, NULL, &termios, &win)) == -1) {
		ERROR("%s", strerror(errno));
		return (NULL);
	} else if (sshpid == 0) { /* child process*/
		/* close unused ends of communication pipes */
		close(retval->fd_output);

		/* set ends of communication pipes to standard input/output */
		if (dup2(ssh_in, STDIN_FILENO) == -1) {
			ERROR("%s", strerror(errno));
			exit(-1);
		}

		/* drop privileges if any set by the application */
		newgid = getgid();
		oldgid = getegid();
		newuid = getuid();
		olduid = geteuid();
		/* if root privileges are to be dropped, pare down the ancillary groups */
		if (olduid == 0) {
			setgroups(1, &newgid);
		}
		/* drop group privileges */
		if (newgid != oldgid) {
#if !defined(linux)
			setegid(newgid);
			setgid(newgid);
#else
			setregid(newgid, newgid);
#endif
		}
		/* drop user privileges */
		if (newuid != olduid) {
#if !defined(linux)
			seteuid(newuid);
			setuid(newuid);
#else
			setreuid(newuid, newuid);
#endif
		}

		/* run ssh with parameters to start ssh subsystem on the server */
		execl(SSH_PROG, SSH_PROG, "-l", username, "-p", port_s, "-s", host, "netconf", NULL);
		ERROR("Executing ssh failed");
		exit(-1);
	} else { /* parent process*/
		DBG("child proces with PID %d forked", (int) sshpid);
		close(ssh_in);
		/* open stream to ssh pseudo terminal */
		/* write there only password/commands for ssh, commands for
		 netopeer-agent are written only through communication pipes
		 (i.e. communication->out file stream). Output from
		 netopeer-agent is then read from ssh pseudo terminal
		 (i.e. communication->in file stream).
		 */
		retval->fd_input = ssh_fd;
		retval->f_input = fdopen(dup(ssh_fd), "a+");
		buffer[0] = '\0';
		/* Read 1 char at a time and build up a string */
		/* This will wait forever until "<" as xml message start is found... */
		DBG("waiting for a password request");
		while ((count++ < BUFFER_SIZE) && (fgets(tmpchar, 2, retval->f_input) != NULL)) {
			strcat(buffer, tmpchar);
			if (((int *) strcasestr(buffer, "password") != NULL) || ((int *) strcasestr(buffer, "enter passphrase"))) {
				/* read rest of the line */
				while ((count++ < BUFFER_SIZE) && (buffer[strlen(buffer) - 1] != ':')) {
					if (fgets(tmpchar, 2, retval->f_input) == NULL) {
						break;
					}
					strcat(buffer, tmpchar);
				}
				DBG("writing the password to ssh");
				//s = callbacks.sshauth_password(username, host);

				fprintf(stdout, "%s ", buffer);
				s = NULL;
				if (system("stty -echo") == -1) {
					ERROR("system() call failed (%s:%d).", __FILE__, __LINE__);
					return (NULL);
				}
				if (getline(&s, &n, stdin) == -1) {
					ERROR("getline() failed (%s:%d).", __FILE__, __LINE__);
					return (NULL);
				}
				if (system("stty echo") == -1) {
					ERROR("system() call failed (%s:%d).", __FILE__, __LINE__);
					return (NULL);
				}

				if (s == NULL) {
					ERROR("Unable to get the password from a user (%s)", strerror(errno));
					return (NULL);
				}
				fprintf(retval->f_input, "%s", s);
				//fprintf(retval->f_input, "\n");
				fflush(retval->f_input);

				/* remove password from the memory */
				memset(s, 0, strlen(s));
				free(s);

				strcpy(buffer, "\0");
				count = 0; /* reset search string */
			}
			if (((int *) strcasestr(buffer, "connecting (yes/no)?") != NULL) || ((int *) strcasestr(buffer, "'yes' or 'no':") != NULL)) {
				switch (forced) {
				case 1:
					fprintf(retval->f_input, "yes");
					DBG("connecting to an unauthenticated host");
					break;
				case 0:
					fprintf(stdout, "%s ", buffer);
					if (fgets(line, 81, stdin) == NULL) {
						WARN("fgets() failed (%s:%d).", __FILE__, __LINE__);
						fprintf(retval->f_input, "no");
						VERB("connecting to an unauthenticated host disabled");
					} else {
						fprintf(retval->f_input, "%s", line);
					}
					break;
				case -1:
					fprintf(stdout, "%s ", buffer);
					fprintf(retval->f_input, "no");
					VERB("connecting to an unauthenticated host disabled");
					break;
				default:
					return (NULL);
				}
				fprintf(retval->f_input, "\n");
				fflush(retval->f_input);
				if (fgets(line, 81, retval->f_input) == NULL); /* read written line from terminal */
				line[0] = '\0'; /* and forget */
				strcpy(buffer, "\0");
				count = 0; /* reset search string */
			}
			if ((int *) strcasestr(buffer, "to the list of known hosts.") != NULL) {
				if (forced != 1) {
					fprintf(stdout, "%s\n", buffer);
					fflush(stdout);
				}
				strcpy(buffer, "\0");
				count = 0; /* reset search string */
			}
			if ((int *) strcasestr(buffer, "No route to host") != NULL) {
				ERROR("%s", buffer);
				return (NULL);
			}
			if ((int *) strcasestr(buffer, "Permission denied") != NULL) {
				ERROR("%s", buffer);
				return (NULL);
			}
			if ((int *) strcasestr(buffer, "Connection refused") != NULL) {
				ERROR("%s", buffer);
				return (NULL);
			}
			if ((int *) strcasestr(buffer, "Connection closed") != NULL) {
				ERROR("%s", buffer);
				return (NULL);
			}
			if ((int *) strcasestr(buffer, "<") != NULL) {
				DBG("XML message begin found, waiting for the password finished");
				ungetc(buffer[strlen(buffer) - 1], retval->f_input);
				break; /* while */
			}
			/* print out other messages */
			if ((int *) strcasestr(buffer, "\n") != NULL) {
				fprintf(stdout, "%s", buffer);
				fflush(stdout);
				strcpy(buffer, "\0");
				count = 0; /* reset search string */
			}
		}
		termios.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
		termios.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR | INPCK | ISTRIP | IXON | PARMRK);
		termios.c_oflag &= ~OPOST;
		termios.c_cc[VMIN] = 1;
		termios.c_cc[VTIME] = 0;

		tcsetattr(retval->fd_input, TCSANOW, &termios);
	}
#else
	int i, j;
	int sock = -1;
	int auth = 0;
	struct addrinfo hints, *res_list, *res;
	char *userauthlist;
	char *err_msg;

	/* get current user to locate SSH known_hosts file */
	pw = getpwuid(getuid());
	if (pw == NULL) {
		if (username == NULL || strisempty(username)) {
			/* unable to get correct username (errno from getpwuid) */
			ERROR("Unable to set a username for the SSH connection (%s).", strerror(errno));
			return (NULL);
		}
		/* guess home dir */
		if (asprintf(&knownhosts_dir, "/home/%s/.ssh", username) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			knownhosts_dir = NULL;
		}
	} else {
		if (username == NULL) {
			username = pw->pw_name;
		}

		if (asprintf(&knownhosts_dir, "%s/.ssh", pw->pw_dir) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			knownhosts_dir = NULL;
		}
	}

	/* Connect to SSH server */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	i = getaddrinfo(host, port_s, &hints, &res_list);
	if (i != 0) {
		ERROR("Unable to translate the host address (%s).", gai_strerror(i));
		free(knownhosts_dir);
		return (NULL);
	}

	for (i = 0, res = res_list; res != NULL; res = res->ai_next) {
		sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sock == -1) {
			/* socket was not created, try another resource */
			i = errno;
			continue;
		}

		if (connect(sock, res->ai_addr, res->ai_addrlen) == -1) {
			/* network connection failed, try another resource */
			i = errno;
			close(sock);
			sock = -1;
			continue;
		}

		/* we're done, network connection established */
		break;
	}
	freeaddrinfo(res_list);

	if (sock == -1) {
		ERROR("Unable to connect to the server (%s).", strerror(i));
		free(knownhosts_dir);
		return (NULL);
	}

	/* allocate netconf session structure */
	retval = malloc(sizeof(struct nc_session));
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		free(knownhosts_dir);
		return (NULL);
	}
	memset(retval, 0, sizeof(struct nc_session));
	if ((retval->stats = malloc (sizeof (struct nc_session_stats))) == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		free(retval);
		free(knownhosts_dir);
		return NULL;
	}
	retval->libssh2_socket = sock;
	retval->fd_input = -1;
	retval->fd_output = -1;
	retval->hostname = strdup(host);
	retval->username = strdup(username);
	retval->groups = NULL; /* client side does not need this information */
	retval->port = strdup(port_s);
	retval->msgid = 1;
	retval->queue_event = NULL;
	retval->queue_msg = NULL;
	retval->logintime = NULL;
	retval->monitored = 0;
	retval->nacm_recovery = 0; /* not needed/decidable on the client side */
	retval->stats->in_rpcs = 0;
	retval->stats->in_bad_rpcs = 0;
	retval->stats->out_rpc_errors = 0;
	retval->stats->out_notifications = 0;

	if (pthread_mutexattr_init(&mattr) != 0) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		free(retval);
		free(knownhosts_dir);
		return (NULL);
	}
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	retval->mut_libssh2_channels = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	if ((r = pthread_mutex_init(retval->mut_libssh2_channels, &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_mqueue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_equeue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_session), &mattr)) != 0) {
		ERROR("Mutex initialization failed (%s).", strerror(r));
		pthread_mutexattr_destroy(&mattr);
		free(retval);
		free(knownhosts_dir);
		return (NULL);
	}
	pthread_mutexattr_destroy(&mattr);

	/* Create a session instance */
	retval->ssh_session = libssh2_session_init();
	if (retval->ssh_session == NULL) {
		ERROR("Unable to initialize the SSH session.");
		goto shutdown;
	}

	/*
	 * set timeout for libssh2 functions - they are still blocking, but
	 * after the timeout they return with LIBSSH2_ERROR_TIMEOUT and we
	 * can perform appropriate reaction
	 */
	LIBSSH2_SET_TIMEOUT(retval->ssh_session, SSH2_TIMEOUT);

	/*
	 * Set up the SSH session, deprecated variant is libssh2_session_startup()
	 */
	if ((r = LIBSSH2_SESSION_HANDSHAKE(retval->ssh_session, retval->libssh2_socket)) != 0) {
		switch(r) {
		case LIBSSH2_ERROR_SOCKET_NONE:
			s = "Invalid socket";
			break;
		case LIBSSH2_ERROR_BANNER_SEND:
			s = "Unable to send the banner to a remote host";
			break;
		case LIBSSH2_ERROR_KEX_FAILURE:
			s = "Encryption key exchange with the remote host failed";
			break;
		case LIBSSH2_ERROR_SOCKET_SEND:
			s = "Unable to send data on the socket";
			break;
		case LIBSSH2_ERROR_SOCKET_DISCONNECT:
			s = "The socket was disconnected";
			break;
		case LIBSSH2_ERROR_PROTO:
			s = "An invalid SSH protocol response was received on the socket";
			break;
		case LIBSSH2_ERROR_EAGAIN:
			s = "Marked for non-blocking I/O but the call would block";
			break;
		case LIBSSH2_ERROR_TIMEOUT:
			s = "Request timeouted";
			break;
		default:
			s = "Unknown error";
			DBG("Error code %d.", r);
			break;
		}
		ERROR("Starting the SSH session failed (%s)", s);
		goto shutdown;
	}

	if (check_hostkey(host, knownhosts_dir, retval->ssh_session) != 0) {
		ERROR("Checking the host key failed.");
		goto shutdown;
	}
	free(knownhosts_dir);
	knownhosts_dir = NULL;

	/* check what authentication methods are available */
	userauthlist = libssh2_userauth_list(retval->ssh_session, username, strlen(username));
	if (userauthlist != NULL) {
		if (strstr(userauthlist, "password")) {
			if (callbacks.sshauth_password != NULL) {
				auth |= NC_SSH_AUTH_PASSWORD;
			}
		}
		if (strstr(userauthlist, "publickey")) {
			auth |= NC_SSH_AUTH_PUBLIC_KEYS;
		}
		if (strstr(userauthlist, "keyboard-interactive")) {
			if (callbacks.sshauth_interactive != NULL) {
				auth |= NC_SSH_AUTH_INTERACTIVE;
			}
		}
	}
	if ((auth == 0) && (libssh2_userauth_authenticated(retval->ssh_session) == 0)) {
		ERROR("Unable to authenticate to the remote server (Authentication methods not supported).");
		goto shutdown;
	}

	/* select authentication according to preferences */
	for (i = 0; i < AUTH_COUNT; i++) {
		if ((sshauth_pref[i].type & auth) == 0) {
			/* method not supported by server, skip */
			continue;
		}

		if (sshauth_pref[i].value < 0) {
			/* all following auth methods are disabled via negative preference value */
			ERROR("Unable to authenticate to the remote server (supported authentication method(s) are disabled).");
			goto shutdown;
		}

		/* found common authentication method */
		switch (sshauth_pref[i].type) {
		case NC_SSH_AUTH_PASSWORD:
			VERB("Password authentication (host %s, user %s)", host, username);
			s = callbacks.sshauth_password(username, host);
			if (libssh2_userauth_password(retval->ssh_session, username, s) != 0) {
				memset(s, 0, strlen(s));
				libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
				ERROR("Authentication failed (%s)", err_msg);
			}
			memset(s, 0, strlen(s));
			free(s);
			break;
		case NC_SSH_AUTH_INTERACTIVE:
			VERB("Keyboard-interactive authentication");
			if (libssh2_userauth_keyboard_interactive(retval->ssh_session,
					username,
					callbacks.sshauth_interactive) != 0) {
				libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
				ERROR("Authentication failed (%s)", err_msg);
			}
			break;
		case NC_SSH_AUTH_PUBLIC_KEYS:
			VERB ("Publickey athentication");
			/* if publickeys path not provided, try to find them in standard path */
			if (callbacks.publickey_filename[0] == NULL || callbacks.privatekey_filename[0] == NULL) {
				WARN ("No key pair specified. Looking for some in the standard SSH path.");
				if (find_ssh_keys ()) {
					ERROR ("Searching for keys failed.");
					/* error */
					break;
				}
			}

			for (j=0; j<SSH2_KEYS; j++) {
				if (callbacks.privatekey_filename[j] == NULL) {
					/* key not available */
					continue;
				}

				VERB("Trying to authenticate using %spair %s %s",
						callbacks.key_protected[j] ? "password-protected " : "", callbacks.privatekey_filename[j], callbacks.publickey_filename[j]);

				if (callbacks.key_protected[j]) {
					s = callbacks.sshauth_passphrase(username, host, callbacks.privatekey_filename[j]);
				} else {
					s = NULL;
				}

				if (libssh2_userauth_publickey_fromfile(retval->ssh_session,
					username, callbacks.publickey_filename[j], callbacks.privatekey_filename[j], s) != 0) {
					libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);

					/* clear the password string */
					if (s != NULL) {
						memset(s, 0, strlen(s));
						free(s);
					}

					ERROR("Authentication failed (%s)", err_msg);
				} else {
					/* clear the password string */
					if (s != NULL) {
						memset(s, 0, strlen(s));
						free(s);
					}
					break;
				}
			}
			break;
		}
		if (libssh2_userauth_authenticated(retval->ssh_session) != 0) {
			break;
		}
	}

	/* check a state of authentication */
	if (libssh2_userauth_authenticated(retval->ssh_session) == 0) {
		ERROR("Authentication failed.");
		goto shutdown;
	}

	/* open a channel */
	retval->ssh_channel = libssh2_channel_open_session(retval->ssh_session);
	if (retval->ssh_channel == NULL) {
		libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
		ERROR("Opening the SSH channel failed (%s)", err_msg);
		goto shutdown;
	}

	/* execute the NETCONF subsystem on the channel */
	if (libssh2_channel_subsystem(retval->ssh_channel, "netconf")) {
		libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
		ERROR("Starting the netconf SSH subsystem failed (%s)", err_msg);
		goto shutdown;
	}
#endif /* not DISABLE_LIBSSH */

	retval->status = NC_SESSION_STATUS_WORKING;

	if (cpblts == NULL) {
		if ((client_cpblts = nc_session_get_cpblts_default()) == NULL) {
			VERB("Unable to set the client's NETCONF capabilities.");
			goto shutdown;
		}
	} else {
		client_cpblts = nc_cpblts_new((const char* const*)(cpblts->list));
	}

	if (nc_client_handshake(retval, client_cpblts->list) != 0) {
		goto shutdown;
	}

	/* set with-defaults capability flags */
	parse_wdcap(retval->capabilities, &(retval->wd_basic), &(retval->wd_modes));

	/* cleanup */
	nc_cpblts_free(client_cpblts);

	return (retval);

shutdown:

	/* cleanup */
	nc_session_close(retval, NC_SESSION_TERM_OTHER);
	nc_session_free(retval);
	nc_cpblts_free(client_cpblts);

	free(knownhosts_dir);

	return (NULL);
}


struct nc_session *nc_session_connect_channel(struct nc_session *session, const struct nc_cpblts* cpblts)
{
#ifdef DISABLE_LIBSSH
	ERROR("%s: SSH channels are provided only with libssh2.", __func__);
	return (NULL);
#else
	struct nc_session *retval, *session_aux;
	struct nc_cpblts *client_cpblts = NULL;
	pthread_mutexattr_t mattr;
	char* err_msg;
	int r;

	if (session == NULL || session->is_server) {
		/* we can open channel only for client-side, no-dummy sessions */
		ERROR("Invalid session for opening another channel");
		return (NULL);
	}

	/* allocate netconf session structure */
	retval = malloc(sizeof(struct nc_session));
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		return (NULL);
	}
	memset(retval, 0, sizeof(struct nc_session));
	if ((retval->stats = malloc (sizeof (struct nc_session_stats))) == NULL) {
		ERROR("Memory allocation failed (%s)", strerror(errno));
		free(retval);
		return NULL;
	}

	retval->is_server = 0;
	retval->libssh2_socket = session->libssh2_socket;
	retval->fd_input = -1;
	retval->fd_output = -1;
	retval->hostname = session->hostname;
	retval->username = session->username;
	retval->groups = NULL; /* client side does not need this information */
	retval->port = session->port;
	retval->msgid = 1;
	retval->queue_event = NULL;
	retval->queue_msg = NULL;
	retval->logintime = NULL;
	session->ntf_active = 0;
	retval->monitored = 0;
	retval->nacm_recovery = 0; /* not needed/decidable on the client side */
	retval->stats->in_rpcs = 0;
	retval->stats->in_bad_rpcs = 0;
	retval->stats->out_rpc_errors = 0;
	retval->stats->out_notifications = 0;

	/* shared resources with the original session */
	retval->ssh_session = session->ssh_session;
	/*
	 * libssh2 is quite stupid - it provides multiple channels inside a single
	 * session, but it does not allow multiple threads to work with these
	 * channels, so we have to share mutex of the master
	 * session to control access to each SSH channel
	 */
	retval->mut_libssh2_channels = session->mut_libssh2_channels;

	if (pthread_mutexattr_init(&mattr) != 0) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	if ((r = pthread_mutex_init(&(retval->mut_mqueue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_equeue), &mattr)) != 0 ||
			(r = pthread_mutex_init(&(retval->mut_session), &mattr)) != 0) {
		ERROR("Mutex initialization failed (%s).", strerror(r));
		pthread_mutexattr_destroy(&mattr);
		return (NULL);
	}
	pthread_mutexattr_destroy(&mattr);

	/* open a separated channel */
	retval->ssh_channel = libssh2_channel_open_session(retval->ssh_session);
	if (retval->ssh_channel == NULL) {
		libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
		ERROR("Opening the SSH channel failed (%s)", err_msg);
		goto shutdown;
	}

	/* execute the NETCONF subsystem on the channel */
	if (libssh2_channel_subsystem(retval->ssh_channel, "netconf")) {
		libssh2_session_last_error(retval->ssh_session, &err_msg, NULL, 0);
		ERROR("Starting the netconf SSH subsystem failed (%s)", err_msg);
		goto shutdown;
	}
	retval->status = NC_SESSION_STATUS_WORKING;

	if (cpblts == NULL) {
		if ((client_cpblts = nc_session_get_cpblts_default()) == NULL) {
			VERB("Unable to set the client's NETCONF capabilities.");
			goto shutdown;
		}
	} else {
		client_cpblts = nc_cpblts_new((const char* const*)(cpblts->list));
	}

	if (nc_client_handshake(retval, client_cpblts->list) != 0) {
		goto shutdown;
	}

	/* set with-defaults capability flags */
	parse_wdcap(retval->capabilities, &(retval->wd_basic), &(retval->wd_modes));

	/* cleanup */
	nc_cpblts_free(client_cpblts);

	/*
	 * link sessions:
	 * session <-> retval <-> session_aux
	 */
	session_aux = session->next;
	if (session_aux != NULL) {
		session_aux->prev = retval;
	}
	session->next = retval;
	retval->next = session_aux;
	retval->prev = session;

	return (retval);

shutdown:

	/* cleanup */
	if (retval->ssh_channel != NULL) {
		libssh2_channel_free(retval->ssh_channel);
	}
	if (retval) {
		free(retval->stats);
		if (retval->mut_libssh2_channels != NULL) {
			pthread_mutex_destroy(retval->mut_libssh2_channels);
			free(retval->mut_libssh2_channels);
			retval->mut_libssh2_channels = NULL;
		}
		pthread_mutex_destroy(&(retval->mut_mqueue));
		pthread_mutex_destroy(&(retval->mut_equeue));
		pthread_mutex_destroy(&(retval->mut_session));
		nc_cpblts_free(retval->capabilities);
		free(retval);
	}
	nc_cpblts_free(client_cpblts);

	return (NULL);

#endif /* not DISABLE_LIBSSH */
}

