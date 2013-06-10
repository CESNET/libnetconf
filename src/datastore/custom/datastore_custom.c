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

static struct ncds_lockinfo lockinfo_running = {NC_DATASTORE_RUNNING, NULL, NULL};
static struct ncds_lockinfo lockinfo_startup = {NC_DATASTORE_STARTUP, NULL, NULL};
static struct ncds_lockinfo lockinfo_candidate = {NC_DATASTORE_CANDIDATE, NULL, NULL};

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

	free(lockinfo_running.sid);
	free(lockinfo_running.time);
	free(lockinfo_startup.sid);
	free(lockinfo_startup.time);
	free(lockinfo_candidate.sid);
	free(lockinfo_candidate.time);
}

int ncds_custom_rollback(struct ncds_ds* ds) {
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;

	return c_ds->callbacks->rollback(c_ds->data);
}

static struct ncds_lockinfo* get_lockinfo(NC_DATASTORE target)
{
	switch (target) {
	case NC_DATASTORE_RUNNING:
		return (&lockinfo_running);
	case NC_DATASTORE_STARTUP:
		return (&lockinfo_startup);
	case NC_DATASTORE_CANDIDATE:
		return (&lockinfo_candidate);
	default:
		return (NULL);
	}
}

const struct ncds_lockinfo* ncds_custom_get_lockinfo(struct ncds_ds* ds, NC_DATASTORE target) {
	int retval;
	const char *sid, *date;
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;
	struct ncds_lockinfo *linfo;

	linfo = get_lockinfo(target);
	if (linfo == NULL) {
		ERROR("%s: invalid target.", __func__);
		return (NULL);
	}

	retval = c_ds->callbacks->is_locked(c_ds->data, target, &sid, &date);
	if (retval < 0) {
		/* not implemented or error, use own information */
		return (linfo);
	} else {
		/* fill updated information */
		free(linfo->sid);
		free(linfo->time);
		linfo->sid = strdup(sid);
		linfo->time = strdup(date);
	}

	return (linfo);
}


int ncds_custom_lock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error) {
	int retval;
	const char *sid = NULL;
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;
	struct ncds_lockinfo *linfo;

	linfo = get_lockinfo(target);
	if (linfo == NULL) {
		ERROR("%s: invalid target.", __func__);
		*error = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
		return (EXIT_FAILURE);
	}

	/* check current status of the lock */
	retval = c_ds->callbacks->is_locked(c_ds->data, target, &sid, NULL);
	if (retval < 0) {
		/* not implemented or error, use own information */
		if (linfo->sid != NULL) {
			/* datastore is already locked */
			retval = 1;
			sid = linfo->sid;
		}
	} /* else ignore internal information that can be invalid */

	if (retval == 0) {
		/* datastore is not locked, try to lock it */
		retval = c_ds->callbacks->lock(c_ds->data, target, session->session_id, error);
		if (retval == EXIT_SUCCESS) {
			linfo->time = nc_time2datetime(time(NULL));
			linfo->sid = strdup(session->session_id);
		}
	} else { /* retval == 1 */
		/* datastore is already locked */
		*error = nc_err_new(NC_ERR_LOCK_DENIED);
		nc_err_set(*error, NC_ERR_PARAM_INFO_SID, sid);
		retval = EXIT_FAILURE;
	}

	return (retval);
}

int ncds_custom_unlock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error) {
	int retval;
	const char *sid;
	struct ncds_ds_custom *c_ds = (struct ncds_ds_custom *) ds;
	struct ncds_lockinfo *linfo;

	linfo = get_lockinfo(target);
	if (linfo == NULL) {
		ERROR("%s: invalid target.", __func__);
		*error = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
		return (EXIT_FAILURE);
	}

	/* check current status of the lock */
	retval = c_ds->callbacks->is_locked(c_ds->data, target, &sid, NULL);
	if (retval < 0) {
		/* not implemented or error, use own information */
		if (linfo->sid == NULL) {
			/* datastore is not locked */
			retval = 0;
		} else {
			sid = linfo->sid;
		}
	} /* else ignore internal information that can be invalid */

	if (retval == 0) {
		/* datastore is not locked, wtf? */
		*error = nc_err_new(NC_ERR_OP_FAILED);
		nc_err_set(*error, NC_ERR_PARAM_MSG, "Target datastore is not locked.");
		retval = EXIT_FAILURE;
	} else { /* retval == 1 */
		/* datastore is locked, check that we can unlock it and do it */
		if (strcmp(sid, session->session_id) != 0) {
			/* datastore is locked by someone else */
			*error = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(*error, NC_ERR_PARAM_MSG, "Target datastore is locked by another session.");
			retval = EXIT_FAILURE;
		} else {
			/* we have locked the datastore, so now we are allowed to unlock it */
			retval = c_ds->callbacks->unlock(c_ds->data, target, error);
			if (retval == EXIT_SUCCESS) {
				free(linfo->time);
				free(linfo->sid);
				linfo->time = NULL;
				linfo->sid = NULL;
			}
		}

	}

	return (retval);
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
