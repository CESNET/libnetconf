/**
 * \file datastore_internal.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief NETCONF datastore internal function prototypes and structures.
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

#ifndef DATASTORE_INTERNAL_H_
#define DATASTORE_INTERNAL_H_

#include "../datastore.h"

struct ncds_lockinfo {
	NC_DATASTORE datastore;
	char* sid;
	char* time;
};

struct ncds_funcs {
	int (*init) (struct ncds_ds* ds);
	void (*free)(struct ncds_ds* ds);
	const struct ncds_lockinfo* (*get_lockinfo)(struct ncds_ds* ds, NC_DATASTORE target);
	int (*lock)(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);
	int (*unlock)(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);
	char* (*getconfig)(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);
	int (*copyconfig)(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, NC_DATASTORE source, char* config, struct nc_err** error);
	int (*deleteconfig)(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error);
	int (*editconfig)(struct ncds_ds *ds, const struct nc_session * session, NC_DATASTORE target, const char * config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);
};

struct ncds_ds {
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
	 * @brief Pointer to a callback function implementing retrieving of the
	 * device status data.
	 */
	char* (*get_state)(const char* model, const char* running, struct nc_err ** e);
	/**
	 * @brief Datastore implementation functions.
	 */
	struct ncds_funcs func;
};

/**
 * @brief Generate unique datastore id
 * @return unique datastore id
 */
ncds_id generate_id (void);

/**
 * @brief Merge two XML documents with a common data model.
 * @param first First XML document to merge.
 * @param second Second XML document to merge.
 * @param data_model XML document containing data model of the merged documents
 * in YIN format.
 * @return Resulting XML document merged from input documents. It is up to the
 * caller to free the memory with xmlFreeDoc().
 */
xmlDocPtr ncxml_merge (const xmlDocPtr first, const xmlDocPtr second, const xmlDocPtr data_model);

#endif /* DATASTORE_INTERNAL_H_ */
