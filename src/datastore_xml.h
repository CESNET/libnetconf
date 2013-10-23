/**
 * \file datastore.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief NETCONF datastore handling function prototypes and structures.
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

#ifndef DATASTORE_XML_H_
#define DATASTORE_XML_H_

#include "datastore.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup store
 * @brief Create a new datastore structure of the specified implementation type with get_state function using libxml2.
 * @param[in] type Datastore implementation type for the new datastore structure.
 * @param[in] model_path Base name of the configuration data model files.
 * libnetconf expects model_path.yin as a data model, model_path.rng for
 * grammar and data types validation, model_path.dsrl for default values
 * validation and model_path.sch for semantic validation.
 * @param[in] get_state Pointer to a callback function that returns a  XML document
 * containing the state data of the device. The parameters it receives are
 * a configuration data model in YIN format and the current content of the running
 * datastore. If NULL is set, \<get\> operation is performed in the same way
 * as \<get-config\>.
 * @return Prepared (not configured) datastore structure. To configure the
 * structure, caller must use the parameter setters of the specific datastore
 * implementation type. Then, the datastore can be initiated (ncds_init()) and
 * used to access the configuration data.
 */
struct ncds_ds* ncds_new2(NCDS_TYPE type, const char * model_path, xmlDocPtr (*get_state)(const xmlDocPtr model, const xmlDocPtr running, struct nc_err **e));

#ifdef __cplusplus
}
#endif

#endif /* DATASTORE_XML_H_ */
