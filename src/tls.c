/**
 * \file tls.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to connect to NETCONF server over TLS.
 *
 * Copyright (c) 2014 CESNET, z.s.p.o.
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

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pwd.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include "netconf_internal.h"

/* global SSL context */
SSL_CTX *ssl_ctx = NULL;

int nc_tls_init()
{
	if (ssl_ctx) {
		VERB("TLS subsystem already initiated.");
		return (EXIT_SUCCESS);
	}

	/* init OpenSSL */
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	SSL_library_init();

	/* prepare global SSL context, allow only mandatory TLS 1.2  */
	if ((ssl_ctx = SSL_CTX_new(TLSv1_2_client_method())) == NULL) {
		ERROR("Unable to create OpenSSL context (%s)", ERR_reason_error_string(ERR_get_error()));
		return (EXIT_FAILURE);
	}

	if(! SSL_CTX_load_verify_locations(ssl_ctx, NC_WORKINGDIR_PATH"/certs/TrustStore.pem", NULL))	{
		ERROR("Loading a trust store from \'%s\' failed (%s).", NC_WORKINGDIR_PATH"/certs/TrustStore.pem", ERR_reason_error_string(ERR_get_error()));
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

int nc_tls_cert(const char* cert, const char* key)
{
	const char* key_ = key;

	if (!ssl_ctx && nc_tls_init() != EXIT_SUCCESS) {
		return (EXIT_FAILURE);
	}

	if (cert == NULL) {
		ERROR("%s: Invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	/* get peer certificate */
	if (SSL_CTX_use_certificate_file(ssl_ctx, cert, SSL_FILETYPE_PEM) != 1) {
		ERROR("Loading a peer certificate from \'%s\' failed (%s).", cert, ERR_reason_error_string(ERR_get_error()));
		return (EXIT_FAILURE);
	}

	if (key_ == NULL) {
		/*
		 * if the file with private key not specified, expect that the private
		 * key is stored altogether with the certificate
		 */
		key_ = cert;
	}
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_, SSL_FILETYPE_PEM) != 1) {
		ERROR("Loading a peer certificate from \'%s\' failed (%s).", key_, ERR_reason_error_string(ERR_get_error()));
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

struct nc_session *nc_session_connect_tls_socket(const char* username, const char* host, int sock)
{
	struct nc_session *retval;
	struct passwd *pw;
	pthread_mutexattr_t mattr;
	int verify, r;

	if (ssl_ctx == NULL) {
		ERROR("TLS subsystem not initiated.");
		return (NULL);
	}

	/* get current user if username not explicitely specified */
	if (username == NULL || strisempty(username)) {
		pw = getpwuid(getuid());
		if (pw == NULL) {
			/* unable to get correct username (errno from getpwuid) */
			ERROR("Unable to set a username for the SSH connection (%s).", strerror(errno));
			return (NULL);
		}
		username = pw->pw_name;
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

	/* prepare a new TLS structure */
	if ((retval->tls = SSL_new(ssl_ctx)) == NULL) {
		ERROR("%s: Unable to prepare TLS structure (%s)", __func__, ERR_reason_error_string(ERR_get_error()));
		free(retval->stats);
		free(retval);
		return (NULL);
	}

	/* connect SSL with existing socket */
	SSL_set_fd(retval->tls, sock);

	/* Set the SSL_MODE_AUTO_RETRY flag to allow OpenSSL perform re-handshake automatically */
	SSL_set_mode(retval->tls, SSL_MODE_AUTO_RETRY);

	/* connect and perform the handshake */
	if (SSL_connect(retval->tls) != 1) {
		ERROR("Connecting over TLS failed (%s).", ERR_reason_error_string(ERR_get_error()));
		SSL_free(retval->tls);
		free(retval->stats);
		free(retval);
		return (NULL);
	}

	/* check certificate checking */
	verify = SSL_get_verify_result(retval->tls);
	switch (verify) {
	case X509_V_OK:
		VERB("Server certificate successfully verified.");
		break;
	default:
		WARN("I'm not happy with the server certificate (error code %d).", verify);;
	}

	/* fill session structure */
	retval->libssh2_socket = sock;
	retval->fd_input = -1;
	retval->fd_output = -1;
	retval->username = strdup(username);
	retval->groups = NULL; /* client side does not need this information */
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
		return (NULL);
	}
	pthread_mutexattr_destroy(&mattr);

	return (retval);
}

/* definition in transport.c */
int transport_connect_socket(const char* username, const char* host, const char* port);

/*
 * libssh2 variant - use internal SSH client implementation using libssh2
 */
struct nc_session *nc_session_connect_tls(const char* username, const char* host, const char* port)
{
	struct nc_session *retval = NULL;
	int sock = -1;

	sock = transport_connect_socket(username, host, port);
	if (sock == -1) {
		return (NULL);
	}

	retval = nc_session_connect_tls_socket(username, host, sock);
	if (retval != NULL) {
		retval->hostname = strdup(host);
		retval->port = strdup(port);
	} else {
		close(sock);
	}

	return (retval);
}

/* from transport.c */
struct nc_session *nc_session_accept_generic(const struct nc_cpblts* capabilities, const char* username);

struct nc_session *nc_session_accept_tls(const struct nc_cpblts* capabilities, X509 *cert)
{
	char common_name[256];
	char *subj;
	char *cn, *aux;
	int len;

	if (cert == NULL) {
		/* try to get information from environment variable */
		subj = getenv("SSL_CLIENT_DN");
		if (!subj) {
			/* we are not able to get correct username */
			ERROR("Missing \'SSL_CLIENT_DN\' enviornment variable, unable to get username.");
			return (NULL);
		}
		/* parse subject to get CN */
		cn = strstr(subj, "CN=");
		if (!cn) {
			ERROR("Client certificate does not include commonName, unable to get username.");
			return (NULL);
		}
		cn = cn + 3;
		aux = strchr(cn, '/');
		if (aux != NULL) {
			len = aux - cn;
			strncpy(common_name, cn, len);
			common_name[len] = '\0';
		} else {
			strncpy(common_name, cn, 256);
			common_name[255] = '\0';
		}
	} else {
		/* get username from certificate directly */
		X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, common_name, 256);
		common_name[255] = '\0';
	}

	return (nc_session_accept_generic(capabilities, common_name));
}
