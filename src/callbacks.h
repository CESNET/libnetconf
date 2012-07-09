/**
 * \file callbacks.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions to set application's callbacks.
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

#ifndef CALLBACKS_H_
#define CALLBACKS_H_

#include <libssh2.h>

/**
 * @brief Set callback function for printing libnetconf's messages.
 * @ingroup genAPI
 * @param[in] func Callback function to use.
 */
void nc_callback_print(int (*func)(const char* msg));

/**
 * @brief Set callback function to process (e.g. print) NETCONF \<rpc-error\> message items.
 * @ingroup reply
 * @param[in] func Callback function to use. Passed parameters are:
 * - tag - error tag,
 * - type - error layer where the error occurred,
 * - severity - error severity,
 * - apptag - the data-model-specific or implementation-specific error condition, if one exists,
 * - path - XPATH expression identifying element with error,
 * - message - human description of the error,
 * - attribute - name of the data-model-specific XML attribute that caused the error,
 * - element - name of the data-model-specific XML element that caused the error,
 * - ns - name of the unexpected XML namespace that caused the error,
 * - sid - session ID of session holding requested lock.
 */
void nc_callback_error_reply(void (*func)(const char* tag,
		const char* type,
		const char* severity,
		const char* apptag,
		const char* path,
		const char* message,
		const char* attribute,
		const char* element,
		const char* ns,
		const char* sid));

/**
 * @brief Set callback function for passing user credentials into libssh2's
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
 * @brief Set callback function for passing user password into libssh2's
 * password authentication method when connecting to 'hostname' as 'username'.
 * @ingroup session
 * @param[in] func Callback function to use.
 */
void nc_callback_sshauth_password(char* (*func)(const char* username,
		const char* hostname));

/**
 * @brief Set callback function for passing user password into libssh2's
 * publickey authentication method when connecting to 'hostname' as 'username'.
 * @ingroup session
 * @param[in] func Callback function to use.
 */
void nc_callback_sshauth_passphrase(char* (*func)(const char* username,
		const char* hostname, const char* priv_key_file));

/**
 * @ingroup session
 * @brief Set callback function to authorize authenticity of an unknown host.
 * @param[in] func Callback function to use.
 */
void nc_callback_ssh_host_authenticity_check(int (*func)(const char* hostname,
		int keytype, const char* fingerprint));

/**
 * @brief Set path to publickey file used in case of SSH authentication via
 * publickey mechanism.
 * @ingroup session
 * @param[in] path Path to the file to use.
 */
void nc_set_publickey_path(const char* path);

/**
 * @brief Set path to privatekey file used in case of SSH authentication via
 * publickey mechanism.
 * @ingroup session
 * @param[in] path
 */
void nc_set_privatekey_path(const char* path);

#endif /* CALLBACKS_H_ */
