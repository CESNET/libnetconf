/**
 * \file edit-config.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Header file for NETCONF edit-config implementation.
 *
 * Copyright (C) 2011-2012 CESNET, z.s.p.o.
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

#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "../netconf.h"
#include "../error.h"

#ifndef EDIT_CONFIG_H_
#define EDIT_CONFIG_H_

typedef xmlXPathObjectPtr keyList;
#define keyListFree(x) xmlXPathFreeObject((xmlXPathObjectPtr)x)

keyList get_keynode_list(xmlDocPtr model);

/**
 * \brief Perform edit-config changes according to given parameters
 *
 * \param[in] repo XML document to change (target NETCONF repository).
 * \param[in] edit edit-config's \<config\> element as XML document defining changes to perform.
 * \param[in] model XML form (YIN) of the configuration data model appropriate to the given repo.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[in] errop NETCONF edit-config's error option defining reactions to an error.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and err structure is filled. Zero is
 * returned on success.
 */
int edit_config(xmlDocPtr repo, xmlDocPtr edit, xmlDocPtr model, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);

int edit_merge (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys);

#endif /* EDIT_CONFIG_H_ */
