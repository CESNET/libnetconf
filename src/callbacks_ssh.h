/**
 * \file callbacks_ssh.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions to set libssh2's callbacks.
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

#ifndef CALLBACKS_SSH_H_
#define CALLBACKS_SSH_H_

#ifndef DISABLE_LIBSSH

#include <libssh2.h>

#include "netconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set a callback function for passing user credentials into the libssh2's
 * keyboard-interactive authentication method
 * @ingroup session
 * @param[in] func Callback function to use.
 */
void nc_callback_sshauth_interactive(void (*func)(const char* name,
		int name_len,
		const char* instruction,
		int instruction_len,
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void** abstract));

/**
 * @brief Set a callback function for passing the user password into the libssh2's
 * password authentication method when connecting to 'hostname' as 'username'.
 * @ingroup session
 * @param[in] func Callback function to use.
 */
void nc_callback_sshauth_password(char* (*func)(const char* username,
		const char* hostname));

/**
 * @brief Set a callback function for passing the user password into the libssh2's
 * publickey authentication method when connecting to 'hostname' as 'username'.
 * @ingroup session
 * @param[in] func Callback function to use.
 */
void nc_callback_sshauth_passphrase(char* (*func)(const char* username,
		const char* hostname, const char* priv_key_file));

/**
 * @ingroup session
 * @brief Set a callback function to authorize authenticity of an unknown host.
 * @param[in] func Callback function to use.
 */
void nc_callback_ssh_host_authenticity_check(int (*func)(const char* hostname,
		int keytype, const char* fingerprint));

/**
 * @brief Set path to a private and a public key file used in case of SSH authentication via
 * a publickey mechanism.
 * @ingroup session
 * @param[in] private
 * @param[in] public
 */
void nc_set_keypair_path(const char* private, const char * public);

#ifdef __cplusplus
}
#endif

#endif /* DISABLE_LIBSSH */

#endif /* CALLBACKS_SSH_H_ */
