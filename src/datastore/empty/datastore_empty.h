/**
 * \file datastore_empty.h
 * \author David Kupka <dkupka@cesnet.cz>
 * \brief NETCONF datastore handling function prototypes and structures for state only devices
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

#ifndef DATASTORE_EMPTY_H_
#define DATASTORE_EMPTY_H_

#include "../../netconf_internal.h"
#include "../datastore_internal.h"

/** \ todo implement function */
struct ncds_ds_empty {
	/**
	 * @brief Datastore implementation type
	 */
	NCDS_TYPE type;
	/**
	 * @brief Datastore ID: 0 - uninitiated datastore, positive value - valid ID
	 */
	ncds_id id;
	/**
	 * @brief Time of the last access to the configuration datastore.
	 */
	time_t last_access;
	/**
	 * @brief Pointer to a callback function implementing the retrieval of the
	 * device status data.
	 */
	char* (*get_state)(const char* model, const char* running, struct nc_err ** e);
	/**
	 * @brief Datastore implementation functions.
	 */
	struct ncds_funcs func;
	/**
	 * @brief Information about data model linked with the datastore
	 */
	struct data_model data_model;
	/**
	 * @brief TransAPI information
	 */
	struct transapi transapi;
};

/**
 * @brief Initialization of an empty datastore
 *
 * @param ds Datastore to initialize
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int ncds_empty_init (struct ncds_ds * ds);

/**
 * @brief Closes and removes the datastore
 *
 * @param ds Datastore to close
 */
void ncds_empty_free (struct ncds_ds * ds);

int ncds_empty_changed(struct ncds_ds* ds);

const struct ncds_lockinfo *ncds_empty_lockinfo(struct ncds_ds* ds, NC_DATASTORE target);

int ncds_empty_lock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

int ncds_empty_unlock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

char* ncds_empty_getconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

int ncds_empty_copyconfig(struct ncds_ds* ds, const struct nc_session* session, const nc_rpc* rpc, NC_DATASTORE target, NC_DATASTORE source, char* config, struct nc_err** error);

int ncds_empty_deleteconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);

int ncds_empty_editconfig(struct ncds_ds *ds, const struct nc_session * session, const nc_rpc* rpc, NC_DATASTORE target, const char * config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);

#endif /* DATASTORE_EMPTY_H_ */
