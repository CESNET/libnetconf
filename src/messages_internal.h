/**
 * \file messages.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Internal functions to create NETCONF messages.
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

#ifndef MESSAGES_INTERNAL_H_
#define MESSAGES_INTERNAL_H_

#include "netconf_internal.h"

/**
 * @brief Create client's \<hello\> message.
 * @ingroup internalAPI
 * @param caps List of client's capabilities.
 * @return rpc structure with the created client's \<hello\> message.
 */
nc_rpc *nc_msg_client_hello(char **caps);

/**
 * @brief Create server's \<hello\> message.
 * @ingroup internalAPI
 * @param session Session structure connected with this hello message.
 * @param caps List of server's capabilities.
 * @return rpc structure with the created server's \<hello\> message.
 */
nc_rpc *nc_msg_server_hello(struct nc_session *session, char **caps);

/**
 * @ingroup internalAPI
 * @brief Create <close-session> NETCONF rpc message.
 *
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_closesession();

void nc_msg_free(struct nc_msg *msg);

struct nc_msg *nc_msg_dup(struct nc_msg *msg);

#endif /* MESSAGES_INTERNAL_H_ */
