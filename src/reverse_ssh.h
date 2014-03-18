/**
 * \file reverse_ssh.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions to connect NETCONF server to a NETCONF client (Reverse SSH).
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

#ifndef REVERSE_SSH_H_
#define REVERSE_SSH_H_

#include <stdint.h>

#include "netconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup callhome
 * @brief Structure describing a management server where the NETCONF server connects to.
 *
 * The structure is public, but, except moving inside the list to specify the
 * server which should be used as first one in nc_session_reverse_connect(),
 * it is HIGHLY recommended to use nc_session_reverse_mngmt_server_*() functions
 * to manipulate with the management servers list.
 */
struct nc_mngmt_server {
	struct addrinfo *addr;
	struct nc_mngmt_server* next;
};

/**
 * @ingroup callhome
 * @brief Add a new management server specification to the end of a list.
 *
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in] list Current list where the server description will be added. If
 * NULL, a new list is created and returned by the function.
 * @param[in] host Host name of the management server. It specifies either a
 * numerical network address (for IPv4, numbers-and-dots notation as supported
 * by inet_aton(3); for IPv6, hexadecimal string format as supported by
 * inet_pton(3)), or a network host-name, whose network addresses are looked up
 * and resolved.
 * @param[in] port Port of the management server. If this argument is a service
 * name (see services(5)), it is translated to the corresponding port number.
 * @return NULL on error, created/modified management servers list.
 */
struct nc_mngmt_server *nc_callhome_mngmt_server_add(struct nc_mngmt_server* list, const char* host, const char* port);

/**
 * @ingroup callhome
 * @brief Remove the specified management server description from the list.
 *
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in,out] list Management servers list to be modified.
 * @param[in,out] remove Management server to be removed from the given list.
 * The structure itself is not freed, use nc_session_reverse_mngmt_server_free()
 * to free it after calling nc_session_reverse_mngmt_server_rm().
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int nc_callhome_mngmt_server_rm(struct nc_mngmt_server* list, struct nc_mngmt_server* remove);

/**
 * @ingroup callhome
 * @brief Free a management server description structure(s). The function doesn't
 * free only the item refered by given pointer, but the complete list of
 * management servers is freed.
 *
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in] list List of management servers to be freed.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int nc_callhome_mngmt_server_free(struct nc_mngmt_server* list);

#ifndef DISABLE_LIBSSH

/**
 * @ingroup callhome
 * @brief Start listening on client side for incoming Reverse SSH connection.
 *
 * This function is not available with configure's --disable-libssh2 option.
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in] port Port number where to listen.
 * @return EXIT_SUCCESS or EXIT_FAILURE on error.
 */
int nc_callhome_listen(unsigned int port);

/**
 * @ingroup callhome
 * @brief Stop listening on client side for incoming Reverse SSH connection.
 *
 * This function is not available with configure's --disable-libssh2 option.
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE if libnetconf is not listening.
 */
int nc_callhome_listen_stop(void);

/**
 * @ingroup callhome
 * @brief Accept incoming Reverse SSH connection and create NETCONF session on it.
 *
 * This function is not available with configure's --disable-libssh2 option.
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in] username Name of the user to login to the server. The user running the
 * application (detected from the effective UID) is used if NULL is specified.
 * @param[in] cpblts NETCONF capabilities structure with capabilities supported
 * by the client. Client can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @return Structure describing the NETCONF session or NULL in case of an error.
 */
struct nc_session *nc_callhome_accept(const char *username, const struct nc_cpblts* cpblts);

#endif /* not DISABLE_LIBSSH */

/**
 * @ingroup callhome
 * @brief Connect NETCONF server to a management center (NETCONF client) using
 * Reverse SSH mechanism
 *
 * To make this function available, you have to include libnetconf_ssh.h.
 *
 * @param[in] host_list List of management servers descriptions where the
 * function will try to connect to. The list can be (is expected to be) ring
 * to allow keep trying to connect to the server(s).
 * @param[in] reconnect_secs Time delay in seconds between connection attempts
 * (even to the same server but it depends on reconnect_count). See
 * /netconf/ssh/call-home/applications/application/reconnect-strategy/interval-secs
 * value in ietf-netconf-server YANG data model.
 * @param[in] reconnect_count Number times the function tries to connect to a
 * single server before moving on to the next server in the host_list. See
 * /netconf/ssh/call-home/applications/application/reconnect-strategy/count-max
 * value in ietf-netconf-server YANG data model.
 * @param sshd_path Optional parameter to specify path to the OpenSSH server.
 * If not specified, default path '/usr/sbin/sshd' is used if parameter is NULL.
 * @return -1 on error. In case of success, function forks the current process
 * running SSH daemon and returns its PID.
 */
int nc_callhome_connect(struct nc_mngmt_server *host_list, uint8_t reconnect_secs, uint8_t reconnect_count, const char* sshd_path);

#ifdef __cplusplus
}
#endif

#endif /* REVERSE_SSH_H_ */
