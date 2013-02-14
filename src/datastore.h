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
	NCDS_TYPE_EMPTY, /**< No datastore. For read-only devices. */
	NCDS_TYPE_FILE /**< Datastores implemented as files */
} NCDS_TYPE;

/**
 * @ingroup store
 * @brief Datastore ID.
 *
 * Each datastore gets its ID after initialisation (ncds_init()). Only
 * initialised datastores can be used to access the configuration data.
 */
typedef int ncds_id;

/**
 * @ingroup store
 * @brief Datastore ID to access libnetconf's internal datastores such as
 * notifications, monitoring, etc.
 */
#define NCDS_INTERNAL_ID 0

/**
 * @ingroup store
 * @brief Datastore structure
 */
struct ncds_ds;

/**
 * @ingroup store
 * @brief Create a new datastore structure of the specified implementation type.
 * @param[in] type Datastore implementation type for the new datastore structure.
 * @param[in] model_path Path to the YIN configuration data model.
 * @param[in] get_state Pointer to a callback function that returns a serialized
 * XML document containing the state configuration data of the device. The parameters
 * it receives are a serialized configuration data model in YIN format and the current
 * content of the running datastore. If NULL is set, \<get\> operation is
 * performed in the same way as \<get-config\>.
 * @return Prepared (not configured) datastore structure. To configure the
 * structure, caller must use the parameter setters of the specific datastore
 * implementation type. Then, the datastore can be initiated (ncds_init()) and
 * used to access the configuration data.
 */
struct ncds_ds* ncds_new(NCDS_TYPE type, const char* model_path, char* (*get_state)(const char* model, const char* running, struct nc_err ** e));

/**
 * @ingroup store
 * @brief Assign the path of the datastore file into the datastore structure.
 *
 * Checks if the file exist and is accessible for reading and writing.
 * If the file does not exist, it is created. The file is opened and the file
 * descriptor is stored in the structure.
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
 * @return Positive integer with the datastore ID on success, negative value on
 * error.
 * 	-1 Invalid datastore
 * 	-2 Type-specific initialization failed
 * 	-3 Unsupported datastore type 
 * 	-4 Memory allocation problem
 */
ncds_id ncds_init(struct ncds_ds* datastore);

/**
 * @ingroup store
 * @brief Close the specified datastore and free all the resources.
 *
 * Equivalent function to ncds_free2().
 *
 * @param[in] datastore Datastore to be closed.
 */
void ncds_free(struct ncds_ds* datastore);

/**
 * @ingroup store
 * @brief Close specified datastore and free all the resources.
 *
 * Equivalent function to ncds_free().
 *
 * @param[in] datastore_id ID of the datastore to be closed.
 */
void ncds_free2(ncds_id datastore_id);

/**
 * @ingroup store
 * @brief Return value of ncds_apply_rpc() when the requested operation is
 * not applicable to the specified datastore.
 */
#define NCDS_RPC_NOT_APPLICABLE ((void *) -1)

/**
 * @ingroup store
 * @brief Perform the requested RPC operation on the datastore.
 * @param[in] id Datastore ID. Use 0 to apply request (typically \<get\>) onto
 * the libnetconf's internal datastore.
 * @param[in] session NETCONF session (a dummy session is acceptable) where the
 * \<rpc\> came from. Capabilities checks are done according to this session.
 * @param[in] rpc NETCONF \<rpc\> message specifying requested operation.
 * @return NULL in case of a non-NC_RPC_DATASTORE_* operation type, else
 * \<rpc-reply\> with \<ok\>, \<data\> or \<rpc-error\> according to the type
 * and the result of the requested operation. When the requested operation is
 * not applicable to the specified datastore (e.g. the namespace does not match),
 * NCDS_RPC_NOT_APPLICABLE ((void *) -1)) is returned.
 */
nc_reply* ncds_apply_rpc(ncds_id id, const struct nc_session* session, const nc_rpc* rpc);

/**
 * @ingroup store
 * @brief Remove all the locks that the given session is holding.
 *
 * @param[in] session Session holding locks to remove
 */
void ncds_break_locks (const struct nc_session* session);

/**
 * @ingroup store
 * @brief Return a serialized XML containing the data model in the YIN format
 *
 * @param[in] id ID of the datastore whose data model we want
 *
 * @return String containing YIN model. Caller must free the memory after use.
 */
char * ncds_get_model (ncds_id id);

/**
 * @ingroup store
 * @brief Return path to the file containing the datastore datamodel
 *
 * @param[in] id ID of the datastore whose data model we want
 *
 * @return String containing the path to the file containing the datastore datamodel.
 * The caller must NOT free the memory.
 */
const char * ncds_get_model_path (ncds_id id);


#endif /* DATASTORE_H_ */
