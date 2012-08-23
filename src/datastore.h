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

#ifndef DATASTORE_H_
#define DATASTORE_H_

#include "netconf.h"

/**
 * @ingroup store
 * @brief Datastore implementation types provided by libnetconf
 */
typedef enum {
	NCDS_TYPE_EMPTY, /**< No datastore. For read-only devices. \todo Implement functions */
	NCDS_TYPE_FILE /**< Datastores implemented as files */
} NCDS_TYPE;

/**
 * @ingroup store
 * @brief Datastore ID.
 *
 * Each datastore gets its ID after initialisation (ncds_init()). Only
 * initialised datstores can be used to access configuration data.
 */
typedef int ncds_id;

/**
 * @ingroup store
 * @brief Datastore structure
 */
struct ncds_ds;

/**
 * @ingroup store
 * @brief Create new datastore structure of the specified implementation type.
 * @param[in] type Datastore implementation type for new datastore structure.
 * @param[in] model_path Path to the YIN configuration data model.
 * @param[in] get_state Pointer to a callback function that returns serialized
 * XML document containing state configuration data of the device. As parameters,
 * it receives serialized configuration data model in YIN format and current
 * content of the running datastore. If NULL is set, \<get\> operation is
 * performed in the same way as \<get-config\>.
 * @return Prepared (not configured) datastore structure. To configure the
 * structure, caller must use parameters setters of the specific datastore
 * implementation type. Then, the datastore can be initiated (ncds_init()) and
 * used to access configuration data.
 */
struct ncds_ds* ncds_new(NCDS_TYPE type, const char* model_path, char* (*get_state)(const char* model, const char* running, struct nc_err ** e));

/**
 * @ingroup store
 * @brief Assign path of the datastore file into the datastore structure.
 *
 * Checks if the file exist and is accessible for reading and writing.
 * If the file does not exist it is created. File is opened and file
 * descriptor is stored in the structure
 *
 * @param[in] datastore Datastore structure to be configured.
 * @param[in] path File path to the file storing configuration datastores.
 * @return 0 on success
 * 	  -1 Invalid datastore
 *	  -2 Invalid path ((does not exist && can not be created) || insufficient rights)
 */
int ncds_file_set_path (struct ncds_ds* datastore, const char* path);

/**
 *
 * @ingroup store
 * @brief Activate datastore structure for use.
 *
 * The datastore configuration is checked and if everything is correct,
 * datastore gets its unique ID to be used for datastore operations
 * (ncds_apply_rpc()).
 *
 * @param[in] datastore Datastore to be initiated.
 * @return Positive integer with datastore ID on success, negative value on
 * error.
 * 	-1 Invalid datastore
 * 	-2 Type-specific initialization failed
 * 	-3 Unsupported datastore type 
 * 	-4 Memory allocation problem
 */
ncds_id ncds_init(struct ncds_ds* datastore);

/**
 * @ingroup store
 * @brief Close specified datastore and free all resources.
 *
 * Equivalent function to ncds_free2().
 *
 * @param[in] datastore Datastore to be closed.
 */
void ncds_free(struct ncds_ds* datastore);

/**
 * @ingroup store
 * @brief Close specified datastore and free all resources.
 *
 * Equivalent function to ncds_free().
 *
 * @param[in] datastore_id ID of the datastore to be closed.
 */
void ncds_free2(ncds_id datastore_id);

/**
 * @ingroup store
 * @brief Perform requested RPC operation on the datastore.
 * @param[in] id Datastore ID.
 * @param[in] session NETCONF session (dummy session is acceptable) where the
 * \<rpc\> came from. Capabilities checks are done according to this session.
 * @param[in] rpc NETCONF \<rpc\> message specifying requested operation.
 * @return NULL in case of non NC_RPC_DATASTORE_* operation type, else
 * \<rpc-reply\> with \<ok\>, \<data\> or \<rpc-error\> according to the type
 * and the result of the requested operation.
 */
nc_reply* ncds_apply_rpc(ncds_id id, const struct nc_session* session, const nc_rpc* rpc);

/**
 * @ingroup store
 * @brief Remove all locks that is holding given session
 *
 * @param[in] session Session holding locks to remove
 */
void ncds_break_locks (const struct nc_session* session);

/**
 * @ingroup store
 * @brief Return serialized XML containing data model in YIN format
 *
 * @param[in] id ID of datastore which data model we want
 *
 * @return String containing YIN model. Caller must free memory after use.
 */
char * ncds_get_model (ncds_id id);

/**
 * @ingroup store
 * @brief Return path to file containing datastore datamodel
 *
 * @param[in] id ID of datastore which data model we want
 *
 * @return String containing path to file containing datastore datamodel. Caller must NOT free the memory.
 */
const char * ncds_get_model_path (ncds_id id);


#endif /* DATASTORE_H_ */
