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

#include "../../netconf_internal.h"
#include "../datastore_internal.h"
#include <semaphore.h>

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
	/**
	 * locking structure
	 */
	struct ds_lock_s {
		/**
		 * semaphore pointer
		 */
		sem_t * lock;
		/**
		 * signal set before locked
	 	 */
		sigset_t sigset;
		/**
		 * Am I holding the lock
		 */
		int holding_lock;
	} ds_lock;
};

/**
 * @brief Initialization of file datastore
 *
 * @param[in] file_ds File datastore structure
 * @return 0 on success, non-zero else
 */
int ncds_file_init (struct ncds_ds* ds);

/**
 * @brief Perform get-config on specified repository.
 *
 * @param[in] file_ds File datastore structure from where the data will be obtained.
 * @param[in] session Session originating the request.
 * @param[in] source Datastore (runnign, startup, candidate) to get data from.
 * @param[in] filter NETCONF filter to apply on resulting data.
 * @param[out] error NETCONF error structure describing arised error.
 * @return NULL on error, resulting data on success.
*/
char* ncds_file_getconfig (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE source, const struct nc_filter *filter, struct nc_err** error);

/**
 * @brief Perform lock of specified datastore for specified session.
 *
 * @param[in] file_ds File datastore structure where the lock should be applied.
 * @param[in] session Session originating the request.
 * @param[in] target Datastore (runnign, startup, candidate) to lock.
 * @param[out] error NETCONF error structure describing arised error.
 * @return 0 on success, non-zero on error and error structure is filled.
 */
int ncds_file_lock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

/**
 * @brief Perform unlock of specified datastore for specified session.
 *
 * @param[in] file_ds File datastore structure where the unlock should be applied.
 * @param[in] session Session originating the request.
 * @param[in] target Datastore (runnign, startup, candidate) to unlock.
 * @param[out] error NETCONF error structure describing arised error.
 * @return 0 on success, non-zero on error and error structure is filled.
 */
int ncds_file_unlock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

/**
 * @brief Close specified datastore and free all resources.
 * @param[in] datastore Datastore to be closed.
 */
void ncds_file_free(struct ncds_ds* ds);

/**
 * @brief Copy content of datastore or externally send configuration to other datastore
 *
 * @param ds Pointer to datastore structure
 * @param session Session of which the request is part of
 * @param target Target datastore.
 * @param source Source datastore, if the value is NC_DATASTORE_NONE then next parametr holds configration to copy
 * @param config Configuration to use as source in form of serialized XML.
 * @param error	 Netconf error structure.
 *
 * @return EXIT_SUCCESS when done without problems
 * 	   EXIT_FAILURE when error occured
 */
int ncds_file_copyconfig (struct ncds_ds *ds, struct nc_session *session, NC_DATASTORE target, NC_DATASTORE source, char * config, struct nc_err **error);

/**
 * @brief Delete target datastore
 *
 * @param ds Datastore to delete
 * @param session Session requesting deletition
 * @param target Datastore type
 * @param error Netconf error structure
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int ncds_file_deleteconfig (struct ncds_ds * ds, struct nc_session * session, NC_DATASTORE target, struct nc_err **error);

/**
 * @brief Perform edit config operation
 *
 * @param ds Datastore to edit
 * @param session Session sending edit request
 * @param target Datastore type
 * @param config Edit configuration.
 * @param defop Default edit operation.
 * @param error Netconf error structure
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int ncds_file_editconfig (struct ncds_ds *ds, struct nc_session * session, NC_DATASTORE target, const char * config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);

#endif /* DATASTORE_FILE_H_ */
