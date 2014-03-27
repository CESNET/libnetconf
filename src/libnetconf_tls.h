/**
 * \file libnetconf_tls.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf's header for control openssl.
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

#ifndef LIBNETCONF_TLS_H_
#define LIBNETCONF_TLS_H_

#include <openssl/x509.h>

#include "netconf.h"
#include "transport.h"
#include "callhome.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup tls NETCONF over TLS
 * \brief libnetconf's functions to use TLS
 *
 * Remember, that to make these functions available, libnetconf must be
 * compiled with --enable-tls configure's option.
 */

/**
 * @ingroup tls
 * @brief Set paths to the client certificate and its private key
 *
 * This function takes effect only on client side. It must be used before
 * establishing NETCONF session (including call home) over TLS.
 *
 * @param[in] peer_cert Path to the file containing client certificate
 * @param[in] peer_key Path to the file containing private key for the client
 * certificate. If NULL, key is expected to be stored in the file specified in
 * cert parameter.
 * @param[in] CAfile Location of the CA certificate used to verify the server
 * certificates. For More info, see documentation for
 * SSL_CTX_load_verify_locations() function from OpenSSL.
 * @param[in] CApath Location of the CA certificates used to verify the server
 * certificates. For More info, see documentation for
 * SSL_CTX_load_verify_locations() function from OpenSSL.
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int nc_tls_init(const char* peer_cert, const char* peer_key, const char *CAfile, const char *CApath);

/**
 * @ingroup tls
 * @brief Accept NETCONF session from a client using TLS transport.
 *
 * This functions does the same work as nc_session_accept() except getting
 * username. In the case of TLS transport, username is get from the client
 * certificate.
 *
 * If the --enable-tls-cn configure's option is used, libnetconf tries
 * to get username from SSL_CLIENT_DN environment variable provided e.g. by
 * stunnel. Userneme is expected in commonName field. Note, that this approach
 * is not specified by NETCONF over TLS specification since it accepts all
 * valid certificates with filled commonName field. Normally, NETCONF server
 * has the list of allowed certificates with specified method to map certificate
 * to a username.
 *
 * @param[in] capabilities NETCONF capabilities structure with the capabilities
 * supported by the server. The caller can use nc_session_get_cpblts_default()
 * to get the structure with the list of all the capabilities supported by
 * libnetconf (this is used in case of a NULL parameter).
 * @param[in] cert TLS certificate from a client. If NULL and --enable-tls-cn
 * was used, username is get from SSL_CLIENT_DN environment variable (if
 * available) as described above.
 * @return Structure describing the accepted NETCONF session or NULL in case of
 * an error.
 */
struct nc_session *nc_session_accept_tls(const struct nc_cpblts* capabilities, X509 *cert);

#ifdef __cplusplus
}
#endif

#endif /* LIBNETCONF_H_ */

