/**
 * \file messages.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Functions to create NETCONF messages.
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

#ifndef MESSAGES_H_
#define MESSAGES_H_

#include "netconf.h"

/**
 * @ingroup rpc
 * @brief Create new NETCONF filter of the specified type.
 * @param[in] type Type of the filter.
 * @param[in] filter Filter content.
 * @return Created NETCONF filter structure.
 */
struct nc_filter *nc_filter_new(NC_FILTER_TYPE type, char* filter);

/**
 * @ingroup rpc
 * @brief Destroy filter structure.
 * @param[in] filter Filter to destroy.
 */
void nc_filter_free(struct nc_filter *filter);

/**
 * @brief Get content of the rpc-reply.
 * @ingroup reply
 * @param[in] reply reply message from the server.
 * @return String with the content of the NETCONF's <rpc-reply> element.
 */
char* nc_reply_get_string (const nc_reply *reply);

/**
 * @ingroup reply
 * @brief Get message-id of the given rpc-reply.
 * @param[in] reply rpc-reply message.
 * @return message-id of the given rpc-reply message.
 */
nc_msgid nc_reply_get_msgid(const nc_reply *reply);

/**
 * @ingroup rpc
 * @brief Get message-id of the given rpc.
 * @param[in] rpc rpc message.
 * @return message-id of the given rpc message.
 */
nc_msgid nc_rpc_get_msgid(const nc_rpc *rpc);

/**
 * @ingroup reply
 * @brief Get type of the rpc-reply message.
 *
 * \<rpc-reply\> message can contain \<ok\>, \<rpc-error\> or \<data\>
 *
 * @param[in] reply rpc-reply message
 * @return One of the NC_REPLY_TYPE.
 */
NC_REPLY_TYPE nc_reply_get_type(const nc_reply *reply);

/**
 * @ingroup reply
 * @brief Get content of the \<data\> element in \<rpc-reply\>.
 * @param reply rpc-reply message.
 * @return String with the content of the \<data\> element.
 */
char *nc_reply_get_data(const nc_reply *reply);

/**
 * @ingroup reply
 * @brief Get \<error-message\> string of \<rpc-error\>
 *
 * This function can be used only on nc_reply with NC_REPLY_ERROR type, that
 * can be checked by nc_reply_get_type().
 *
 * @param[in] reply rpc-reply of NC_REPLY_ERROR type
 * @return error-message string or NULL on error.
 */
char *nc_reply_get_errormsg(const nc_reply *reply);

/**
 * @brief Free rpc message.
 * @ingroup rpc
 * @param[in] rpc rpc message to free.
 */
void nc_rpc_free(nc_rpc *rpc);

/**
 * @brief Free reply message.
 * @ingroup reply
 * @param[in] reply reply message to free.
 */
void nc_reply_free(nc_reply *reply);

/**
 * @ingroup rpc
 * @brief Create \<copy-config\> NETCONF rpc message.
 *
 * @param[in] source Source configuration datastore type. If the
 * NC_DATASTORE_NONE is specified, data parameter is used as the complete
 * configuration to copy.
 * @param[in] target Target configuration datastore type to be replaced.
 * @param[in] data If the NC_DATASTORE_NONE is specified as the source, data
 * parameter is used as the complete configuration to copy. For other types of
 * source datastore, this parameter is ignored.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_copyconfig(NC_DATASTORE_TYPE source, NC_DATASTORE_TYPE target, const char *data);

/**
 * @ingroup rpc
 * @brief Create \<delete-config\> NETCONF rpc message.
 *
 * @param[in] target Target configuration datastore type to be deleted.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_deleteconfig(NC_DATASTORE_TYPE target);

/**
 * @ingroup rpc
 * @brief Create \<edit-config\> NETCONF rpc message.
 *
 * @param[in] target Target configuration datastore type to be edited.
 * @param[in] default_operation Default operation for this request, 0 to skip
 * setting this parameter and use default server's ('merge') behavior.
 * @param[in] error_option Set reaction to an error, 0 for the server's default
 * behavior.
 * @param[in] data edit-config operation request description. The content of
 * this parameter is sent to server as a content of the \<config\> element.
 *
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_editconfig(NC_DATASTORE_TYPE target, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, const char *data);

/**
 * @ingroup rpc
 * @brief Create \<get\> NETCONF rpc message.
 *
 * @param[in] filter NETCONF filter or NULL if no filter required.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_get(struct nc_filter *filter);

/**
 * @ingroup rpc
 * @brief Create \<get-config\> NETCONF rpc message.
 *
 * @param[in] source Source configuration datastore type being queried.
 * @param[in] filter NETCONF filter or NULL if no filter required.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_getconfig(NC_DATASTORE_TYPE source, struct nc_filter *filter);

/**
 * @ingroup rpc
 * @brief Create \<kill-session\> NETCONF rpc message.
 *
 * @param[in] kill_sid ID of session to kill.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_killsession(const char *kill_sid);

/**
 * @ingroup rpc
 * @brief Create \<lock\> NETCONF rpc message.
 *
 * @param[in] target Target configuration datastore type to be locked.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_lock(NC_DATASTORE_TYPE target);

/**
 * @ingroup rpc
 * @brief Create \<unlock\> NETCONF rpc message.
 *
 * @param[in] target Target configuration datastore type to be unlocked.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_unlock(NC_DATASTORE_TYPE target);

/**
 * @ingroup rpc
 * @brief Create a generic NETCONF rpc message with specified content.
 *
 * Function gets data parameter and envelope it into \<rpc\> container. Caller
 * is fully responsible for the correctness of the given data.
 *
 * @param[in] data XML content of the \<rpc\> request to be sent.
 * @return Created rpc message.
 */
nc_rpc *nc_rpc_generic(const char* data);

#endif /* MESSAGES_H_ */
