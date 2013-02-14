/**
 * \file nacm.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief NETCONF Access Control Module functions (according to RFC 6536).
 *
 * Copyright (C) 2012-2013 CESNET, z.s.p.o.
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

#ifndef NACM_H_
#define NACM_H_

#include "netconf.h"

#define NACM_DENY true
#define NACM_PERMIT false

/**
 * @brief Init NACM subsystem
 * @return 0 on success, -1 on error
 */
int nacm_init(void);

/**
 * @brief Close NACM internal structures
 */
void nacm_close(void);

/**
 * @brief Refresh internal structures according to the NACM configuration data.
 * @return 0 on success, -1 on error
 */
int nacm_config_refresh(void);

/**
 * @brief Connect current NACM rules with the specified NETCONF RPC
 *
 * This function only prepares NACM structures to be used with the given RPC.
 * No NACM check is done by this function, it only enables further NACM checks
 * according to the NACM rules available at the time of calling nacm_start().
 *
 * @param[in] rpc RPC message where NACM rules will be applied
 * @param[in] session NETCONF session where the RPC came from.
 * @return 0 on success, -1 on error (invalid parameters or memory allocation
 * failure). rpc is not affected in case of error.
 */
int nacm_start(nc_rpc* rpc, const struct nc_session* session);

/**
 * @brief Check if there is a permission to invoke requested protocol operation.
 *
 * @param[in] rpc RPC specifying requested operation
 * @return 0 if the check passed<br/>
 *         1 if the operation is prohibited<br/>
 *        -1 in case of error
 */
int nacm_check_operation(const nc_rpc* rpc);

struct rule_list** nacm_rule_lists_dup(struct rule_list** list);
void nacm_rule_list_free(struct rule_list* rl);

#endif /* NACM_H_ */
