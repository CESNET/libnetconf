/**
 * \file datastore_file.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief NETCONF datastore handling function prototypes and structures for file datastore implementation.
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

#ifndef DATASTORE_FILE_H_
#define DATASTORE_FILE_H_

#include "../datastore_internal.h"

struct ncds_ds_file {
	/**
	 * @brief Datastore implementation type
	 */
	NCDS_TYPE type;
	/**
	 * @brief Datastore ID: 0 - uninitiated datastore, positive value - valid ID
	 */
	ncds_id id;
	/**
	 * @brief Path to file containing YIN configuration data model
	 */
	char* model_path;
	/**
	 * @brief YIN configuration data model in the libxml2's document form.
	 */
	xmlDocPtr model;
	/**
	 * @brief Datastore implementation functions.
	 */
	struct ncds_funcs func;
	/**
	 * @brief Path to file containing configuration data, single file is
	 * used for all datastore types (running, startup, candidate).
	 */
	char* path;
	/**
	 * @brief File descriptor of opened file containing configuration data
	 */
	FILE* file;
	/**
	 * libxml2's document structure of the datastore
	 */
	xmlDocPtr xml;
	/**
	 * libxml2 Node pointers providing access to individual datastores
	 */
	xmlNodePtr candidate, running, startup;
};

/**
 * @brief Initialization of file datastore
 *
 * @param[in] file_ds File datastore structure
 * @return 0 on success, non-zero else
 */
int ncds_file_init (struct ncds_ds* ds);

/**
 * @brief Perform lock of specified datastore for specified session.
 *
 * @param[in] file_ds File datastore structure where the lock should be applied.
 * @param[in] session Session originating the request.
 * @param[in] target Datastore (runnign, startup, candidate) to lock.
 * @return NULL on success, filled NETCONF error structure in case of error.
 */
struct nc_err* ncds_file_lock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target);

/**
 * @brief Perform unlock of specified datastore for specified session.
 *
 * @param[in] file_ds File datastore structure where the unlock should be applied.
 * @param[in] session Session originating the request.
 * @param[in] target Datastore (runnign, startup, candidate) to unlock.
 * @return NULL on success, filled NETCONF error structure in case of error.
 */
struct nc_err* ncds_file_unlock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target);

/**
 * @brief Close specified datastore and free all resources.
 * @param[in] datastore Datastore to be closed.
 */
void ncds_file_free(struct ncds_ds* ds);

#endif /* DATASTORE_FILE_H_ */
