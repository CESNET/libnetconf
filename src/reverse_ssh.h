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

#include "netconf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DISABLE_LIBSSH

/**
 * @ingroup callhome
 * @brief Start listening on client side for incoming Reverse SSH connection.
 *
 * This function is not available with configure's --disable-libssh2 option.
 *
 * @param[in] port Port number where to listen.
 * @return EXIT_SUCCESS or EXIT_FAILURE on error.
 */
int nc_session_reverse_listen(unsigned int port);

/**
 * @ingroup callhome
 * @brief Stop listening on client side for incoming Reverse SSH connection.
 *
 * This function is not available with configure's --disable-libssh2 option.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE if libnetconf is not listening.
 */
int nc_session_reverse_listen_stop(void);

/**
 * @ingroup callhome
 * @brief Accept incoming Reverse SSH connection and create NETCONF session on it.
 *
 * This function is not available with configure's --disable-libssh2 option.
 *
 * @param[in] username Name of the user to login to the server. The user running the
 * application (detected from the effective UID) is used if NULL is specified.
 * @param[in] cpblts NETCONF capabilities structure with capabilities supported
 * by the client. Client can use nc_session_get_cpblts_default() to get the
 * structure with the list of all the capabilities supported by libnetconf (this is
 * used in case of a NULL parameter).
 * @return Structure describing the NETCONF session or NULL in case of an error.
 */
struct nc_session *nc_session_reverse_accept(const char *username, const struct nc_cpblts* cpblts);

#endif /* not DISABLE_LIBSSH */

#ifdef __cplusplus
}
#endif

#endif /* REVERSE_SSH_H_ */
