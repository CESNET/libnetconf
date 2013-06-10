/**
 * \file datastore.c
 * \author Robin Obůrka <robin.oburka@nic.cz>
 * \brief Implementation of NETCONF datastore handling functions.
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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <libxml/tree.h>

#include "../../netconf_internal.h"
#include "../../error.h"
#include "../../session.h"
#include "../../nacm.h"
#include "../../config.h"
#include "../datastore_internal.h"
#include "datastore_custom_private.h"
#include "datastore_custom.h"
#include "../edit_config.h"

static struct ncds_lockinfo lockinfo_running = {NC_DATASTORE_RUNNING, NULL, NULL}; //dummy value for lockinfo

void ncds_custom_set_data(struct ncds_ds* ds, void *custom_data, const struct ncds_custom_funcs *callbacks) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;
	c_ds->data = custom_data;
	c_ds->callbacks = callbacks;
}

int ncds_custom_was_changed(struct ncds_ds* ds) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->was_changed(c_ds->data);
}


int ncds_custom_init(struct ncds_ds* ds) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->init(c_ds->data);
}

void ncds_custom_free(struct ncds_ds* ds) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	//call user's free callback
	c_ds->callbacks->free(c_ds->data);

	//cleanup my things
	free(c_ds);
}

int ncds_custom_rollback(struct ncds_ds* ds) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->rollback(c_ds->data);
}

const struct ncds_lockinfo* ncds_custom_get_lockinfo(struct ncds_ds* ds, NC_DATASTORE target) {

	return &lockinfo_running;
}


int ncds_custom_lock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->lock(c_ds->data, target, error);
}

int ncds_custom_unlock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->unlock(c_ds->data, target, error);
}

char* ncds_custom_getconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE source, struct nc_err** error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->getconfig(c_ds->data, source, error);
}

int ncds_custom_copyconfig(struct ncds_ds *ds, const struct nc_session *session, const nc_rpc* rpc, NC_DATASTORE target, NC_DATASTORE source, char * config, struct nc_err **error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->copyconfig(c_ds->data, target, source, config, error);
}

int ncds_custom_deleteconfig(struct ncds_ds * ds, const struct nc_session * session, NC_DATASTORE target, struct nc_err **error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->deleteconfig(c_ds->data, target, error);
}

int ncds_custom_editconfig(struct ncds_ds *ds, const struct nc_session * session, const nc_rpc* rpc, NC_DATASTORE target, const char * config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->editconfig(c_ds->data, target, config, defop, errop, error);
}
