/**
 * \file datastore.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of NETCONF datastore handling functions.
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "netconf_internal.h"
#include "messages.h"
#include "error.h"
#include "datastore.h"
#include "datastore/datastore_internal.h"
#include "datastore/file/datastore_file.h"
#include "datastore/empty/datastore_empty.h"

struct ncds_ds_list {
	struct ncds_ds *datastore;
	struct ncds_ds_list* next;
};

static struct ncds_ds_list *datastores = NULL;

/**
 * @brief Get ncds_ds_list structure containing storage information with
 * specified ID.
 *
 * @param[in] id ID of the storage.
 * @return Pointer to the required ncds_ds_list structure inside internal
 * datastores variable.
 */
static struct ncds_ds *datastores_get_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;

	for (ds_iter = datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ds_iter->datastore->id == id) {
			break;
		}
	}

	return (ds_iter->datastore);
}

static struct ncds_ds *datastores_detach_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;
	struct ncds_ds_list *ds_prev = NULL;
	struct ncds_ds * retval = NULL;

	for (ds_iter = datastores; ds_iter != NULL; ds_prev = ds_iter, ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ds_iter->datastore->id == id) {
			break;
		}
	}

	if (ds_iter != NULL) {
		/* required datastore was found */
		if (ds_prev == NULL) {
			/* we're removing the first item of the datastores list */
			datastores = ds_iter->next;
		} else {
			ds_prev->next = ds_iter->next;
		}
		retval = ds_iter->datastore;
		free (ds_iter);
	}

	return retval;
}

struct ncds_ds* ncds_new(NCDS_TYPE type, const char* model_path)
{
	struct ncds_ds* ds = NULL;

	if (model_path == NULL) {
		ERROR("%s: missing model path parameter.", __func__);
		return (NULL);
	}

	switch(type) {
	case NCDS_TYPE_FILE:
		ds = (struct ncds_ds*) calloc (1, sizeof(struct ncds_ds_file));
		ds->func.init = ncds_file_init;
		ds->func.free = ncds_file_free;
		ds->func.lock = ncds_file_lock;
		ds->func.unlock = ncds_file_unlock;
		ds->func.getconfig = ncds_file_getconfig;
		break;
	default:
		ERROR("Unsupported datastore implementation required.");
		return (NULL);
	}
	if (ds == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	ds->type = type;

	/* get configuration data model */
	if (access(model_path, R_OK) == -1) {
		ERROR("Unable to access configuration data model %s (%s).", model_path, strerror(errno));
		free(ds);
		return (NULL);
	}
	ds->model = xmlReadFile(model_path, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR);
	if (ds->model == NULL) {
		ERROR("Unable to read configuration data model %s.", model_path);
		free(ds);
		return (NULL);
	}
	ds->model_path = strdup(model_path);

	/* ds->id stays 0 to indicate, that datastore is still not fully configured */

	return (ds);
}

ncds_id generate_id (void)
{
	ncds_id	current_id;
	
	do {
		/* generate id */
		current_id = (rand () + 1) % INT_MAX;
	/* until it's unique */
	} while (datastores_get_ds (current_id) != NULL);

	return current_id;
}

ncds_id ncds_init (struct ncds_ds* datastore)
{
	struct ncds_ds_list * item;

	if (datastore == NULL) {
		return -1;
	}

	/** \todo data model validation */

	/* call implementation-specific datastore init() function */
	datastore->func.init(datastore);
	
	/* acquire unique id */
	datastore->id = generate_id ();

	/* add to list */
	item = malloc (sizeof (struct ncds_ds_list));
	if (item == NULL) {
		return -4;
	}
	item->datastore = datastore;
	item->next = datastores;
	datastores = item;

	return datastore->id;
}

void ncds_free(struct ncds_ds* datastore)
{
	struct ncds_ds *ds = NULL;

	if (datastore == NULL) {
		WARN ("%s: no datastore to free.", __func__);
		return;
	}

	if (datastore->id > 0) {
		/* datastore is initialized and must be in the datastores list */
		ds = datastores_detach_ds(datastore->id);
	} else {
		/* datastore to free is uninitialized and will be only freed */
		ds = datastore;
	}

	/* close and free the datastore itself */
	if (ds != NULL) {
		datastore->func.free(ds);
	}
}

void ncds_free2 (ncds_id datastore_id)
{
	struct ncds_ds *del;

	/* empty list */
	if (datastores == NULL) {
		return;
	}

	/* invalid id */
	if (datastore_id <= 0) {
		WARN("%s: invalid datastore ID to free.", __func__);
		return;
	}

	/* get datastore from the internal datastores list */
	del = datastores_get_ds(datastore_id);

	/* free if any found */
	if (del != NULL) {
		/*
		 * ncds_free() detaches the item from the internal datastores
		 * list, also the whole list item (del variable here) is freed
		 * by ncds_free(), so do not do it here!
		 */
		ncds_free (del);
	}
}

nc_reply* ncds_apply_rpc(ncds_id id, struct nc_session* session, nc_rpc* rpc)
{
	struct nc_err* e = NULL;
	struct ncds_ds* ds;
	char* data = NULL;
	int ret = EXIT_FAILURE;
	nc_reply* reply;

	if (rpc->type.rpc != NC_RPC_DATASTORE_READ && rpc->type.rpc != NC_RPC_DATASTORE_WRITE) {
		return (nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED)));
	}

	ds = datastores_get_ds(id);
	if (ds == NULL) {
		return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
	}

	switch(nc_rpc_get_op(rpc)) {
	case NC_OP_LOCK:
		ret = ds->func.lock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_UNLOCK:
		ret = ds->func.unlock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_GETCONFIG:
		/* todo filtering */
		data = ds->func.getconfig(ds, session, nc_rpc_get_source(rpc), NULL, &e);
		break;
	default:
		ERROR("%s: unsupported basic NETCONF operation requested.", __func__);
		return (nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED)));
		break;
	}

	if (e != NULL) {
		/* operation failed and error is filled */
		reply = nc_reply_error(e);
	} else if (data == NULL && ret != EXIT_SUCCESS) {
		/* operation failed, but no additional information is provided */
		reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
	} else {
		if (data != NULL) {
			reply = nc_reply_data(data);
			free(data);
		} else {
			reply = nc_reply_ok();
		}
	}
	return (reply);
}
