/**
 * \file transport.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions implementing transport layer for NETCONF.
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

#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include "netconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup session
 * @brief Supported NETCONF transport protocols enumeration. To change currently
 * used transport protocol, call nc_session_transport().
 *
 * Note that NC_TRANSPORT_TLS is supported only when libnetconf is compiled
 * with --enable-tls configure's option. If the option is not used,
 * nc_session_transport() returns EXIT_FAILURE with NC_TRANSPORT_TLS value.
 *
 * This setting is valuable only for client side NETCONF applications.
 */
typedef enum NC_TRANSPORT {
	NC_TRANSPORT_UNKNOWN = -1, /**< Unknown transport protocol, this is not acceptable as input value */
	NC_TRANSPORT_SSH, /**< NETCONF over SSH, this value is used by default */
	NC_TRANSPORT_TLS /**< NETCONF over TLS */
} NC_TRANSPORT;

/**
 * @ingroup session
 * @brief Set transport protocol for the sessions created by subsequent
 * nc_session_connect() calls. By default, transport protocol is set to
 * #NC_TRANSPORT_SSH
 *
 * This function is thread-safe. Change made by calling this function applies
 * only to the current thread.
 */
int nc_session_transport(NC_TRANSPORT proto);

/**
 * @ingroup session
 * @brief Create NETCONF session to the specified server.
 *
 * This function can internally use various callbacks set by the client to perform
 * SSH authentication. It selects authentication mechanism from the list
 * provided by the SSH server and based on the preferences set by the client via
 * nc_ssh_pref(). Then, appropriate callback function (set by
 * nc_callback_sshauth_password(), nc_callback_sshauth_passphrase(),
 * nc_set_publickey_path() or nc_set_privatekey_path()) is used to perform the
 * authentication.
 *
 * @param[in] host Hostname or address (both Ipv4 and IPv6 are accepted). 'localhost'
 * is used by default if NULL is specified.
 * @param[in] port Port number of the server. Default value 830 is used if 0 is
 * specified.
 * @param[in] username Name of the user to login to the server. The user running the
 * application (detected from the effective UID) is used if NULL is specified.
 * @param[in] cpblts NETCONF capabilities structure with capabilities supported
 * by the client. Client can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @return Structure describing the NETCONF session or NULL in case of an error.
 */
struct nc_session *nc_session_connect(const char *host, unsigned short port, const char *username, const struct nc_cpblts* cpblts);

/**
 * @ingroup session
 * @brief Create another NETCONF session using already established SSH session.
 * No authentication is needed in this case.
 *
 * This function works only if libnetconf is compiled with using libssh2.
 *
 * @param[in] session Already established NETCONF session.
 * @param[in] cpblts NETCONF capabilities structure with capabilities supported
 * by the client. Client can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @return Structure describing the NETCONF session or NULL in case of an error.
 *
 */
struct nc_session *nc_session_connect_channel(struct nc_session *session, const struct nc_cpblts* cpblts);

/**
 * @ingroup session
 * @brief Accept NETCONF session from a client.
 *
 * The caller process of this function is supposed to be launched as a
 * subprocess of the transport protocol server (in case of SSH, it is called
 * SSH Subsystem). Username assigned to the NETCONF session is guessed from the
 * process's UID. This approach supposes that the transport protocol server
 * launches the caller process with the changed UID according to the user
 * logged in (OpenSSH's sshd does this, stunnel does not - see
 * nc_session_accept_username() instead of this function).
 *
 * Only one NETCONF session can be accepted in a single caller since it
 * communicates with the transport protocol server directly via (redirected)
 * stdin and stdout streams.
 *
 * @param[in] capabilities NETCONF capabilities structure with the capabilities supported
 * by the server. The caller can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @return Structure describing the accepted NETCONF session or NULL in case of an error.
 */
struct nc_session *nc_session_accept(const struct nc_cpblts* capabilities);

/**
 * @ingroup session
 * @brief Accept NETCONF session from a client and assign it to the specified
 * username.
 *
 * The same as nc_session_accept() except that instead of guessing username
 * from the process's UID, the specified username is assigned to the NETCONF
 * session. This can be used especially in case that the transport protocol
 * server (sshd, stunnel,...) does not change process's UID automatically.
 *
 * @param[in] capabilities NETCONF capabilities structure with the capabilities supported
 * by the server. The caller can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @param[in] username Name of the user which will be assigned to the NETCONF
 * session. This information is used for example by NACM subsystem. If NULL,
 * the function act the same way as the nc_session_accept() function.
 * @return Structure describing the accepted NETCONF session or NULL in case of an error.
 */
struct nc_session *nc_session_accept_username(const struct nc_cpblts* capabilities, const char* username);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H_ */
