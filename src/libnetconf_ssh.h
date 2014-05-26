/**
 * \file libnetconf_ssh.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf's header for control libssh2.
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

#ifndef LIBNETCONF_SSH_H_
#define LIBNETCONF_SSH_H_

#include "callbacks_ssh.h"
#include "transport.h"
#include "callhome.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Available SSH authentication mechanisms.
 * @ingroup session
 */
typedef enum
{
	NC_SSH_AUTH_PUBLIC_KEYS = 1, /**< SSH user authorization via publickeys */
	NC_SSH_AUTH_PASSWORD = 2,   /**< SSH user authorization via password */
	NC_SSH_AUTH_INTERACTIVE = 4 /**< interactive SSH user authorization */
} NC_SSH_AUTH_TYPE;

/**
 * @ingroup session
 * @brief Set the preference of the SSH authentication methods.
 *
 * Allowed authentication types are defined as NC_SSH_AUTH_TYPE type.
 * The default preferences are:
 * 1. interactive (3)
 * 2. password (2)
 * 3. public keys (1)
 *
 * This function has no effect with configure's --disable-libssh2 option.
 *
 * To make this function available, you have to include libnetconf_ssh.h header
 * file.
 *
 * @param[in] type Setting preference for the given authentication type.
 * @param[in] preference Preference value. Higher value means higher preference.
 * Negative value disables the given authentication type. On equality of values,
 * the last set authentication type is preferred.
 */
void nc_ssh_pref(NC_SSH_AUTH_TYPE type, short int preference);

#ifdef __cplusplus
}
#endif

#endif /* LIBNETCONF_H_ */

