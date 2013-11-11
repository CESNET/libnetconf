/**
 * \file datastore.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of the NETCONF datastore handling functions.
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
#include <assert.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#ifndef DISABLE_VALIDATION
#  include <libxml/relaxng.h>
#  include <libxslt/transform.h>
#  include <libxslt/xsltInternals.h>
#endif

#include "netconf_internal.h"
#include "messages.h"
#include "messages_xml.h"
#include "messages_internal.h"
#include "error.h"
#include "with_defaults.h"
#include "session.h"
#include "datastore_xml.h"
#include "nacm.h"
#include "url_internal.h"
#include "datastore/edit_config.h"
#include "datastore/datastore_internal.h"
#include "datastore/file/datastore_file.h"
#include "datastore/empty/datastore_empty.h"
#include "datastore/custom/datastore_custom_private.h"
#include "transapi/transapi_internal.h"
#include "config.h"

#ifndef DISABLE_NOTIFICATIONS
#  include "notifications.h"
#endif

#include "../models/ietf-netconf-monitoring.xxd"
#include "../models/ietf-netconf-notifications.xxd"
#include "../models/ietf-netconf-with-defaults.xxd"
#include "../models/nc-notifications.xxd"
#include "../models/ietf-netconf-acm.xxd"
#include "../models/ietf-netconf.xxd"
#include "../models/notifications.xxd"
#include "../models/ietf-inet-types.xxd"
#include "../models/ietf-yang-types.xxd"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

/*
 * From internal.c to be used by nc_session_get_cpblts_deault() to detect
 * what part of libnetconf is initiated and can be executed
 */
extern int nc_init_flags;

extern struct nc_shared_info *nc_info;

/* reserve memory for error pointers such as ERROR_POINTER or NCDS_RPC_NOT_APPLICABLE */
char error_area;
#define ERROR_POINTER ((void*)(&error_area))

char* server_capabilities = NULL;

struct ncds_ds_list {
	struct ncds_ds *datastore;
	struct ncds_ds_list* next;
};

struct ds_desc {
	NCDS_TYPE type;
	char* filename;
};

struct ncds {
	struct ncds_ds_list *datastores;
	ncds_id* datastores_ids;
	int count;
	int array_size;
};

struct ncds_ds *nacm_ds = NULL; /* for NACM subsystem */
static struct ncds ncds = {NULL, NULL, 0, 0};
static struct model_list *models_list = NULL;
static char** models_dirs = NULL;

char* get_state_nacm(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e));
char* get_state_monitoring(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e));
static int get_model_info(xmlXPathContextPtr model_ctxt, char **name, char **version, char **namespace, char **prefix, char ***rpcs, char ***notifs);
static struct data_model* get_model(const char* module, const char* version);
static int ncds_features_parse(struct data_model* model);
static int ncds_update_uses_groupings(struct data_model* model);
static int ncds_update_uses_augments(struct data_model* model);
extern int first_after_close;

#ifndef DISABLE_NOTIFICATIONS
char* get_state_notifications(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e));
#endif

static struct ncds_ds *datastores_get_ds(ncds_id id);

/* Allocate and fill the ncds func structure based on the type. */
static struct ncds_ds* ncds_fill_func(NCDS_TYPE type)
{
	struct ncds_ds* ds;
	switch (type) {
	case NCDS_TYPE_CUSTOM:
		if ((ds = (struct ncds_ds*) calloc(1, sizeof(struct ncds_ds_custom))) == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (NULL);
		}
		ds->func.init = ncds_custom_init;
		ds->func.free = ncds_custom_free;
		ds->func.was_changed = ncds_custom_was_changed;
		ds->func.rollback = ncds_custom_rollback;
		ds->func.get_lockinfo = ncds_custom_get_lockinfo;
		ds->func.lock = ncds_custom_lock;
		ds->func.unlock = ncds_custom_unlock;
		ds->func.getconfig = ncds_custom_getconfig;
		ds->func.copyconfig = ncds_custom_copyconfig;
		ds->func.deleteconfig = ncds_custom_deleteconfig;
		ds->func.editconfig = ncds_custom_editconfig;
		break;
	case NCDS_TYPE_FILE:
		if ((ds = (struct ncds_ds*) calloc(1, sizeof(struct ncds_ds_file))) == NULL ) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (NULL );
		}
		ds->func.init = ncds_file_init;
		ds->func.free = ncds_file_free;
		ds->func.was_changed = ncds_file_changed;
		ds->func.rollback = ncds_file_rollback;
		ds->func.get_lockinfo = ncds_file_lockinfo;
		ds->func.lock = ncds_file_lock;
		ds->func.unlock = ncds_file_unlock;
		ds->func.getconfig = ncds_file_getconfig;
		ds->func.copyconfig = ncds_file_copyconfig;
		ds->func.deleteconfig = ncds_file_deleteconfig;
		ds->func.editconfig = ncds_file_editconfig;
		break;
	case NCDS_TYPE_EMPTY:
		if ((ds = (struct ncds_ds*) calloc(1, sizeof(struct ncds_ds_empty))) == NULL ) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (NULL );
		}
		ds->func.init = ncds_empty_init;
		ds->func.free = ncds_empty_free;
		ds->func.was_changed = ncds_empty_changed;
		ds->func.rollback = ncds_empty_rollback;
		ds->func.get_lockinfo = ncds_empty_lockinfo;
		ds->func.lock = ncds_empty_lock;
		ds->func.unlock = ncds_empty_unlock;
		ds->func.getconfig = ncds_empty_getconfig;
		ds->func.copyconfig = ncds_empty_copyconfig;
		ds->func.deleteconfig = ncds_empty_deleteconfig;
		ds->func.editconfig = ncds_empty_editconfig;
		break;
	default:
		ERROR("Unsupported datastore implementation required.");
		return (NULL );
	}
	return (ds);
}

#ifndef DISABLE_NOTIFICATIONS
#define INTERNAL_DS_COUNT 9
#define MONITOR_DS_INDEX 3
#define NOTIF_DS_INDEX_L 4
#define NOTIF_DS_INDEX_H 6
#define WD_DS_INDEX 7
#define NACM_DS_INDEX 8
#else
#define INTERNAL_DS_COUNT 6
#define MONITOR_DS_INDEX 3
#define WD_DS_INDEX 4
#define NACM_DS_INDEX 5
#endif
int internal_ds_count = 0;
int ncds_sysinit(int flags)
{
	int i;
	struct ncds_ds *ds;
	struct ncds_ds_list *dsitem;
	struct model_list *list_item;

	unsigned char* model[INTERNAL_DS_COUNT] = {
			ietf_inet_types_yin,
			ietf_yang_types_yin,
			ietf_netconf_yin,
			ietf_netconf_monitoring_yin,
#ifndef DISABLE_NOTIFICATIONS
			ietf_netconf_notifications_yin,
			nc_notifications_yin,
			notifications_yin,
#endif
			ietf_netconf_with_defaults_yin,
			ietf_netconf_acm_yin
	};
	unsigned int model_len[INTERNAL_DS_COUNT] = {
			ietf_inet_types_yin_len,
			ietf_yang_types_yin_len,
			ietf_netconf_yin_len,
			ietf_netconf_monitoring_yin_len,
#ifndef DISABLE_NOTIFICATIONS
			ietf_netconf_notifications_yin_len,
			nc_notifications_yin_len,
			notifications_yin_len,
#endif
			ietf_netconf_with_defaults_yin_len,
			ietf_netconf_acm_yin_len
	};
	char* (*get_state_funcs[INTERNAL_DS_COUNT])(const char* model, const char* running, struct nc_err ** e) = {
			NULL, /* ietf-inet-types */
			NULL, /* ietf-yang-types */
			NULL, /* ietf-netconf */
			get_state_monitoring, /* ietf-netconf-monitoring */
#ifndef DISABLE_NOTIFICATIONS
			get_state_notifications, /* ietf-netconf-notifications */
			NULL, /* nc-notifications */
			NULL, /* notifications */
#endif
			NULL, /* ietf-netconf-with-defaults */
			get_state_nacm /* NACM status data */
	};
	struct ds_desc internal_ds_desc[INTERNAL_DS_COUNT] = {
			{NCDS_TYPE_EMPTY, NULL},
			{NCDS_TYPE_EMPTY, NULL},
			{NCDS_TYPE_EMPTY, NULL},
			{NCDS_TYPE_EMPTY, NULL},
#ifndef DISABLE_NOTIFICATIONS
			{NCDS_TYPE_EMPTY, NULL}, /* ietf-netconf-notifications */
			{NCDS_TYPE_EMPTY, NULL}, /* nc-notifications */
			{NCDS_TYPE_EMPTY, NULL}, /* notifications */
#endif
			{NCDS_TYPE_EMPTY, NULL},
			{NCDS_TYPE_FILE, NC_WORKINGDIR_PATH"/datastore-acm.xml"}
	};
#ifndef DISABLE_VALIDATION
	char* relaxng_validators[INTERNAL_DS_COUNT] = {
			NULL, /* ietf-inet-types */
			NULL, /* ietf-yang-types */
			NULL, /* ietf-netconf */
			NULL, /* ietf-netconf-monitoring */
#ifndef DISABLE_NOTIFICATIONS
			NULL, /* ietf-netconf-notifications */
			NULL, /* nc-notifications */
			NULL, /* notifications */
#endif
			NULL, /* ietf-netconf-with-defaults */
			NC_WORKINGDIR_PATH"/ietf-netconf-acm-data.rng" /* NACM RelaxNG schema */
	};
	char* schematron_validators[INTERNAL_DS_COUNT] = {
			NULL, /* ietf-inet-types */
			NULL, /* ietf-yang-types */
			NULL, /* ietf-netconf */
			NULL, /* ietf-netconf-monitoring */
#ifndef DISABLE_NOTIFICATIONS
			NULL, /* ietf-netconf-notifications */
			NULL, /* nc-notifications */
			NULL, /* notifications */
#endif
			NULL, /* ietf-netconf-with-defaults */
			NC_WORKINGDIR_PATH"/ietf-netconf-acm-schematron.xsl" /* NACM Schematron XSL stylesheet */
	};
#endif

	internal_ds_count = 0;
	for (i = 0; i < INTERNAL_DS_COUNT; i++) {
		if ((i == NACM_DS_INDEX) && !(flags & NC_INIT_NACM)) {
			/* NACM is not enabled */
			continue;
		}

		if ((i == MONITOR_DS_INDEX) && !(flags & NC_INIT_MONITORING)) {
			/* NETCONF monitoring is not enabled */
			continue;
		}

		if ((i == WD_DS_INDEX) && !(flags & NC_INIT_WD)) {
			/* NETCONF with-defaults capability is not enabled */
			continue;
		}

#ifndef DISABLE_NOTIFICATIONS
		if ((i >= NOTIF_DS_INDEX_L && i <= NOTIF_DS_INDEX_H) && !(flags & NC_INIT_NOTIF)) {
			/* Notifications are not enabled */
			continue;
		}
#endif

		ds = ncds_fill_func(internal_ds_desc[i].type);
		if (ds == NULL) {
			/* The error was reported already. */
			return (EXIT_FAILURE);
		}
		ds->type = internal_ds_desc[i].type;
		if (ds->type == NCDS_TYPE_FILE && ncds_file_set_path(ds, internal_ds_desc[i].filename) != 0) {
			ERROR("Linking internal datastore to a file (%s) failed.", internal_ds_desc[i].filename);
			return (EXIT_FAILURE);
		}

		ds->id = internal_ds_count++;

		ds->data_model = malloc(sizeof(struct data_model));
		if (model == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}

		ds->data_model->xml = xmlReadMemory ((char*)model[i], model_len[i], NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		if (ds->data_model->xml == NULL ) {
			ERROR("Unable to read the internal monitoring data model.");
			free (ds);
			return (EXIT_FAILURE);
		}

		/* prepare xpath evaluation context of the model for XPath */
		if ((ds->data_model->ctxt = xmlXPathNewContext(ds->data_model->xml)) == NULL) {
			ERROR("%s: Creating XPath context failed.", __func__);
			/* with-defaults cannot be found */
			return (EXIT_FAILURE);
		}
		if (xmlXPathRegisterNs(ds->data_model->ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
			xmlXPathFreeContext(ds->data_model->ctxt);
			return (EXIT_FAILURE);
		}

		if (get_model_info(ds->data_model->ctxt,
				&(ds->data_model->name),
				&(ds->data_model->version),
				&(ds->data_model->namespace),
				&(ds->data_model->prefix),
				&(ds->data_model->rpcs),
				&(ds->data_model->notifs)) != 0) {
			ERROR("Unable to process internal configuration data model.");
			xmlFreeDoc(ds->data_model->xml);
			free(ds);
			return (EXIT_FAILURE);
		}

		ds->data_model->path = NULL;
		ncds_features_parse(ds->data_model);
		ds->data_model->model_tree = NULL;
		ds->ext_model = ds->data_model->xml;

		/* resolve uses statements in groupings and augments definitions */
		ncds_update_uses_groupings(ds->data_model);
		ncds_update_uses_augments(ds->data_model);

		ds->last_access = 0;
		ds->get_state = get_state_funcs[i];

		/* update internal model lists */
		list_item = malloc(sizeof(struct model_list));
		if (list_item == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
		list_item->model = ds->data_model;
		list_item->next = models_list;
		models_list = list_item;

#ifndef DISABLE_VALIDATION
		/* set validation */
		if (relaxng_validators[i] != NULL || schematron_validators[i] != NULL) {
			ncds_set_validation(ds, 1, relaxng_validators[i], schematron_validators[i]);
		}
#endif

		/* init */
		ds->func.init(ds);

		/* add to a datastore list */
		if ((dsitem = malloc (sizeof(struct ncds_ds_list))) == NULL ) {
			return (EXIT_FAILURE);
		}
		if (i == NACM_DS_INDEX) {
			/* provide NACM datastore to the NACM subsystem for faster access */
			nacm_ds = ds;
		}
		dsitem->datastore = ds;
		dsitem->next = ncds.datastores;
		ncds.datastores = dsitem;
		ncds.count++;
		if (ncds.count >= ncds.array_size) {
			void *tmp = realloc(ncds.datastores_ids, (ncds.array_size + 10) * sizeof(ncds_id));
			if (tmp == NULL) {
				ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
				return (EXIT_FAILURE);
			}

			ncds.array_size += 10;
			ncds.datastores_ids = tmp;
		}

		ds = NULL;
	}

	return (EXIT_SUCCESS);
}

/**
 * @brief Get ncds_ds structure from the datastore list containing storage
 * information with the specified ID.
 *
 * @param[in] id ID of the storage.
 * @return Pointer to the required ncds_ds structure inside internal
 * datastores variable.
 */
static struct ncds_ds *datastores_get_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;

	for (ds_iter = ncds.datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ds_iter->datastore->id == id) {
			break;
		}
	}

	if (ds_iter == NULL) {
		return NULL;
	}

	return (ds_iter->datastore);
}

/**
 * @brief Remove datastore with the specified ID from the internal datastore list.
 *
 * @param[in] id ID of the storage.
 * @return Pointer to the required ncds_ds structure detached from the internal
 * datastores variable.
 */
static struct ncds_ds *datastores_detach_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;
	struct ncds_ds_list *ds_prev = NULL;
	struct ncds_ds * retval = NULL;

	if (id == 0) {
		/* ignore a try to detach some uninitialized or internal datastore */
		return (NULL);
	}

	for (ds_iter = ncds.datastores; ds_iter != NULL; ds_prev = ds_iter, ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ds_iter->datastore->id == id) {
			break;
		}
	}

	if (ds_iter != NULL) {
		/* required datastore was found */
		if (ds_prev == NULL) {
			/* we're removing the first item of the datastores list */
			ncds.datastores = ds_iter->next;
		} else {
			ds_prev->next = ds_iter->next;
		}
		retval = ds_iter->datastore;
		free(ds_iter);
		ncds.count--;
	}

	return retval;
}

int ncds_device_init (ncds_id * id, struct nc_cpblts *cpblts, int force)
{
	nc_rpc * rpc_msg = NULL;
	nc_reply * reply_msg = NULL;
	struct ncds_ds_list * ds_iter, *start = NULL;
	struct ncds_ds * ds;
	struct nc_session * dummy_session = NULL;
	struct nc_err * err;
	int nocpblts = 0, ret, retval = EXIT_SUCCESS;
	xmlDocPtr running_doc = NULL;
	char* new_running_config = NULL;
	xmlBufferPtr running_buf = NULL;

	if (id != NULL) {
		/* initialize only the device connected with the given datastore ID */
		if ((ds = datastores_get_ds(*id)) == NULL) {
			ERROR("Unable to find module with id %d", *id);
			return (EXIT_FAILURE);
		}
		start = calloc(1, sizeof(struct ncds_ds_list));
		start->datastore = ds;
	} else {
		/* OR if datastore not specified, initialize all transAPI capable modules */
		start = ncds.datastores;
	}

	if (cpblts == NULL) {
		cpblts = nc_session_get_cpblts_default();
		nocpblts = 1;
	}

	/* create dummy session for applying copy-config (startup->running) */
	if ((dummy_session = nc_session_dummy("dummy-internal", "server", NULL, cpblts)) == NULL) {
		ERROR("%s: Creating dummy-internal session failed.", __func__);
		retval = EXIT_FAILURE;
		goto cleanup;
	}

	if (nocpblts) {
		nc_cpblts_free(cpblts);
		cpblts = NULL;
	}

	rpc_msg = nc_rpc_copyconfig(NC_DATASTORE_STARTUP, NC_DATASTORE_RUNNING);
	running_buf = xmlBufferCreate();

	for (ds_iter=start; ds_iter != NULL; ds_iter=ds_iter->next) {
		if (ds_iter->datastore->transapi.init) {
			/* module can return current configuration of device */
			if (ds_iter->datastore->transapi.init(&running_doc)) {
				ERROR ("init function from module %s failed.", ds_iter->datastore->data_model->name);
				retval = EXIT_FAILURE;
				goto cleanup;
			}
		}

		if (first_after_close || force) {
			/* if this process is first after a nc_close(system=1) or reinitialization is forced */

			/* dump running configuration data returned by transapi_init() */
			if (running_doc == NULL) {
				new_running_config = strdup("");
			} else {
				xmlNodeDump(running_buf, running_doc, xmlDocGetRootElement(running_doc), 0, 0);
				new_running_config = strdup((char*)xmlBufferContent(running_buf));
				xmlBufferEmpty(running_buf);
			}

			/* Clean RUNNING datastore. This is important when transAPI is deployed and does not harm when not. */
			/* It is done by calling low level function to avoid invoking transAPI now. */

			/*
			 * If :startup is not supported, running stays persistent between
			 * reboots
			 */
			if (!nc_cpblts_enabled(dummy_session, NC_CAP_STARTUP_ID)) {
				goto cleanup;
			}

			/* replace running datastore with current configuration provided by module, or erase it if none provided
			 * this is done be low level function to bypass transapi */
			ret = ds_iter->datastore->func.copyconfig(ds_iter->datastore, NULL, NULL, NC_DATASTORE_RUNNING, NC_DATASTORE_CONFIG, new_running_config, &err);
			if (ret != 0 && ret != EXIT_RPC_NOT_APPLICABLE) {
				ERROR("Failed to replace running with current configuration.");
				retval = EXIT_FAILURE;
				goto cleanup;
			}

			/* initial copy of startup to running will cause full (re)configuration of module */
			/* Here is used high level function ncds_apply_rpc to apply startup configuration and use transAPI */
			reply_msg = ncds_apply_rpc(ds_iter->datastore->id, dummy_session, rpc_msg);
			if (reply_msg == NULL || (reply_msg != NCDS_RPC_NOT_APPLICABLE && nc_reply_get_type (reply_msg) != NC_REPLY_OK)) {
				ERROR ("Failed perform initial copy of startup to running.");
				nc_reply_free(reply_msg);
				retval = EXIT_FAILURE;
				goto cleanup;
			}
			nc_reply_free(reply_msg);

			/* prepare variable for the next loop */
			free(new_running_config);
			new_running_config = NULL;
		}

		/* prepare variable for the next loop */
		xmlFreeDoc(running_doc);
		running_doc = NULL;
	}

cleanup:
	xmlBufferFree(running_buf);
	xmlFreeDoc(running_doc);
	free(new_running_config);

	nc_rpc_free(rpc_msg);
	nc_session_close(dummy_session, NC_SESSION_TERM_OTHER);
	nc_session_free(dummy_session);

	if (id != NULL) {
		free(start);
	}

	return (retval);
}


char * ncds_get_model(ncds_id id, int base)
{
	struct ncds_ds * datastore = datastores_get_ds(id);
	xmlBufferPtr buf;
	xmlDocPtr model;
	char * retval = NULL;

	if (datastore == NULL) {
		return (NULL);
	} else if (base) {
		model = datastore->data_model->xml;
	} else {
		model = datastore->ext_model;
	}

	if (model != NULL) {
		buf = xmlBufferCreate();
		xmlNodeDump(buf, model, model->children, 1, 1);
		retval = strdup((char*) xmlBufferContent(buf));
		xmlBufferFree(buf);
	}
	return retval;
}

const char * ncds_get_model_path(ncds_id id)
{
	struct ncds_ds * datastore = datastores_get_ds(id);

	if (datastore == NULL) {
		return NULL;
	}

	return (datastore->data_model->path);
}

int ncds_model_info(const char* path, char **name, char **version, char **namespace, char **prefix, char ***rpcs, char ***notifs)
{
	int retval;
	xmlXPathContextPtr model_ctxt;
	xmlDocPtr model_xml;

	model_xml = xmlReadFile(path, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (model_xml == NULL) {
		ERROR("Unable to read the configuration data model %s.", path);
		return (EXIT_FAILURE);
	}

	/* prepare xpath evaluation context of the model for XPath */
	if ((model_ctxt = xmlXPathNewContext(model_xml)) == NULL) {
		ERROR("%s: Creating XPath context failed.", __func__);
		xmlFreeDoc(model_xml);
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		xmlFreeDoc(model_xml);
		return (EXIT_FAILURE);
	}

	retval = get_model_info(model_ctxt, name, version, namespace, prefix, rpcs, notifs);

	xmlFreeDoc(model_xml);
	xmlXPathFreeContext(model_ctxt);

	return (retval);
}

static int get_model_info(xmlXPathContextPtr model_ctxt, char **name, char **version, char **namespace, char **prefix, char ***rpcs, char ***notifs)
{
	xmlXPathObjectPtr result = NULL;
	xmlChar *xml_aux;
	int i, j, l;

	if (notifs) {*notifs = NULL;}
	if (rpcs) {*rpcs = NULL;}
	if (namespace) { *namespace = NULL;}
	if (prefix) { *prefix = NULL;}
	if (name) {*name = NULL;}
	if (version) {*version = NULL;}

	/* get name of the schema */
	if (name != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				xmlXPathFreeObject (result);
				return (EXIT_FAILURE);
			} else {
				*name = (char*) xmlGetProp (result->nodesetval->nodeTab[0], BAD_CAST "name");
			}
			xmlXPathFreeObject (result);
			if (*name == NULL ) {
				return (EXIT_FAILURE);
			}
		}
	}

	/* get version */
	if (version != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:revision", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				*version = strdup("");
			} else {
				for (i = 0; i < result->nodesetval->nodeNr; i++) {
					xml_aux = xmlGetProp (result->nodesetval->nodeTab[i], BAD_CAST "date");
					if (*version == NULL ) {
						*version = (char*)xml_aux;
					} else if (xml_aux != NULL ) {
						l = strlen (*version); /* should be 10: YYYY-MM-DD */
						if (l != xmlStrlen (xml_aux)) {
							/* something strange happend ?!? - ignore this value */
							continue;
						}
						/* compare with currently the newest version and remember only when it is newer */
						for (j = 0; j < l; j++) {
							if (xml_aux[j] > (*version)[j]) {
								free (*version);
								*version = (char*)xml_aux;
								xml_aux = NULL;
								break;
							} else if (xml_aux[j] < (*version)[j]) {
								break;
							}
						}
						free (xml_aux);
					}
				}
			}
			xmlXPathFreeObject (result);
			if (*version == NULL ) {
				goto errorcleanup;
				return (EXIT_FAILURE);
			}
		}
	}

	/* get namespace of the schema */
	if (namespace != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:namespace", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				xmlXPathFreeObject (result);
				goto errorcleanup;
			} else {
				*namespace = (char*) xmlGetProp (result->nodesetval->nodeTab[0], BAD_CAST "uri");
			}
			xmlXPathFreeObject (result);
			if (*namespace == NULL ) {
				goto errorcleanup;
			}
		}
	}

	/* get prefix of the schema */
	if (namespace != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:prefix", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				*prefix = strdup("");
			} else {
				*prefix = (char*) xmlGetProp (result->nodesetval->nodeTab[0], BAD_CAST "value");
			}
			xmlXPathFreeObject (result);
			if (*prefix == NULL ) {
				goto errorcleanup;
			}
		}
	}

	if (rpcs != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:rpc", model_ctxt);
		if (result != NULL ) {
			if (!xmlXPathNodeSetIsEmpty(result->nodesetval)) {
				*rpcs = malloc((result->nodesetval->nodeNr + 1) * sizeof(char*));
				if (*rpcs == NULL) {
					ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
					xmlXPathFreeObject(result);
					goto errorcleanup;
				}
				for (i = j = 0; i < result->nodesetval->nodeNr; i++) {
					(*rpcs)[j] = (char*)xmlGetProp(result->nodesetval->nodeTab[i], BAD_CAST "name");
					if ((*rpcs)[j] != NULL) {
						j++;
					}
				}
				(*rpcs)[j] = NULL;
			}
			xmlXPathFreeObject (result);
		}
	}

	if (notifs != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:notification", model_ctxt);
		if (result != NULL ) {
			if (!xmlXPathNodeSetIsEmpty(result->nodesetval)) {
				*notifs = malloc((result->nodesetval->nodeNr + 1) * sizeof(char*));
				if (*notifs == NULL) {
					ERROR("Memory allocation failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
					xmlXPathFreeObject(result);
					goto errorcleanup;
				}
				for (i = j = 0; i < result->nodesetval->nodeNr; i++) {
					(*notifs)[j] = (char*)xmlGetProp(result->nodesetval->nodeTab[i], BAD_CAST "name");
					if ((*notifs)[j] != NULL) {
						j++;
					}
				}
				(*notifs)[j] = NULL;
			}
			xmlXPathFreeObject (result);
		}
	}

	return (EXIT_SUCCESS);


errorcleanup:

	xmlFree(*name);
	*name = NULL;
	xmlFree(*version);
	*version = NULL;
	xmlFree(*namespace);
	*namespace = NULL;
	xmlFree(*prefix);
	*prefix = NULL;
	if (*rpcs != NULL) {
		for (i = 0; (*rpcs)[i] != NULL; i++) {
			free((*rpcs)[i]);
		}
		free(*rpcs);
		*rpcs = NULL;
	}
	if (*notifs != NULL) {
		for (i = 0; (*notifs)[i] != NULL; i++) {
			free((*notifs)[i]);
		}
		free(*notifs);
		*notifs = NULL;
	}

	return (EXIT_FAILURE);
}

/* used in ssh.c and session.c */
char **get_schemas_capabilities(void)
{
	struct model_list* listitem;
	int i;
	char **retval = NULL;

	/* get size of the output */
	for (i = 0, listitem = models_list; listitem != NULL; listitem = listitem->next, i++);

	/* prepare output array */
	if ((retval = malloc(sizeof(char*) * (i + 1))) == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	for (i = 0, listitem = models_list; listitem != NULL; listitem = listitem->next, i++) {
		if (asprintf(&(retval[i]), "%s?module=%s%s%s", listitem->model->namespace, listitem->model->name,
				(listitem->model->version != NULL && strnonempty(listitem->model->version)) ? "&amp;revision=" : "",
				(listitem->model->version != NULL && strnonempty(listitem->model->version)) ? listitem->model->version : "") == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			/* move iterator back, then iterator will go back to the current value and will rewrite it*/
			i--;
		}
	}

	retval[i] = NULL;
	return (retval);
}

static char* get_schemas_str(const char* name, const char* version, const char* ns)
{
	char* retval = NULL;
	if (asprintf(&retval,"<schema><identifier>%s</identifier>"
			"<version>%s</version>"
			"<format>yin</format>"
			"<namespace>%s</namespace>"
			"<location>NETCONF</location>"
			"</schema>",
			name,
			version,
			ns) == -1) {
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		retval = NULL;
	}
	return (retval);
}

char* get_schemas()
{
	char *schema = NULL, *schemas = NULL, *aux = NULL;
	struct model_list* listitem;

	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		aux = get_schemas_str(listitem->model->name,
				listitem->model->version,
				listitem->model->namespace);
		if (schema == NULL) {
			schema = aux;
		} else if (aux != NULL) {
			void *tmp = realloc(schema, strlen(schema) + strlen(aux) + 1);
			if (tmp == NULL) {
				ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
				free(aux);
				/* return what we have */
				break;
			} else {
				schema = tmp;
				strcat(schema, aux);
				free(aux);
			}
		}
	}

	if (schema != NULL) {
		if (asprintf(&schemas, "<schemas>%s</schemas>", schema) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			schemas = NULL;
		}
		free(schema);
	}
	return (schemas);
}

#ifndef DISABLE_NOTIFICATIONS
char* get_state_notifications(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e))
{
	char *retval = NULL;

	/*
	 * notifications streams
	 */
	retval = ncntf_status ();
	if (retval == NULL ) {
		retval = strdup("");
	}

	return (retval);
}
#endif /* DISABLE_NOTIFICATIONS */

char* get_state_monitoring(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e))
{
	char *schemas = NULL, *sessions = NULL, *retval = NULL, *ds_stats = NULL, *ds_startup = NULL, *ds_cand = NULL, *stats = NULL, *aux = NULL;
	struct ncds_ds_list* ds = NULL;
	const struct ncds_lockinfo *info;

	/*
	 * datastores
	 */
	/* find non-empty datastore implementation */
	for (ds = ncds.datastores; ds != NULL ; ds = ds->next) {
		if (ds->datastore && ds->datastore->type == NCDS_TYPE_FILE) {
			break;
		}
	}

	if (ds != NULL) {
		/* startup datastore */
		info = ds->datastore->func.get_lockinfo(ds->datastore, NC_DATASTORE_STARTUP);
		if (info != NULL && info->sid != NULL) {
			if (asprintf(&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
					"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				aux = NULL;
			}
		}
		if (asprintf(&ds_startup, "<datastore><name>startup</name>%s</datastore>",
		                (aux != NULL) ? aux : "") == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			ds_startup = NULL;
		}
		free(aux);
		aux = NULL;

		/* candidate datastore */
		info = ds->datastore->func.get_lockinfo(ds->datastore, NC_DATASTORE_CANDIDATE);
		if (info != NULL && info->sid != NULL) {
			if (asprintf(&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
					"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				aux = NULL;
			}
		}
		if (asprintf(&ds_cand, "<datastore><name>candidate</name>%s</datastore>",
		                (aux != NULL) ? aux : "") == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			ds_cand = NULL;
		}
		free(aux);
		aux = NULL;

		/* running datastore */
		info = ds->datastore->func.get_lockinfo (ds->datastore, NC_DATASTORE_RUNNING);
		if (info != NULL && info->sid != NULL ) {
			if (asprintf (&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
					"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				aux = NULL;
			}
		}
		if (asprintf (&ds_stats, "<datastores><datastore><name>running</name>%s</datastore>%s%s</datastores>",
		        (aux != NULL )? aux : "",
		        (ds_startup != NULL) ? ds_startup : "",
		        (ds_cand != NULL) ? ds_cand : "") == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			ds_stats = NULL;
		}
		free (ds_startup);
		free (ds_cand);
		free (aux);
	}

	/*
	 * schemas
	 */
	schemas = get_schemas();

	/*
	 * sessions
	 */
	sessions = nc_session_stats();

	/*
	 * statistics
	 */
	if (nc_info != NULL) {
		pthread_rwlock_rdlock(&(nc_info->lock));
		if (asprintf(&stats, "<statistics><netconf-start-time>%s</netconf-start-time>"
				"<in-bad-hellos>%u</in-bad-hellos>"
				"<in-sessions>%u</in-sessions>"
				"<dropped-sessions>%u</dropped-sessions>"
				"<in-rpcs>%u</in-rpcs>"
				"<in-bad-rpcs>%u</in-bad-rpcs>"
				"<out-rpc-errors>%u</out-rpc-errors>"
				"<out-notifications>%u</out-notifications></statistics>",
				nc_info->stats.start_time,
				nc_info->stats.bad_hellos,
				nc_info->stats.sessions_in,
				nc_info->stats.sessions_dropped,
				nc_info->stats.counters.in_rpcs,
				nc_info->stats.counters.in_bad_rpcs,
				nc_info->stats.counters.out_rpc_errors,
				nc_info->stats.counters.out_notifications) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			stats = NULL;
		}
		pthread_rwlock_unlock(&(nc_info->lock));
	}

	/* get it all together */
	if (asprintf(&retval, "<netconf-state xmlns=\"%s\">%s%s%s%s%s</netconf-state>", NC_NS_MONITORING,
			(server_capabilities != NULL) ? server_capabilities : "",
			(ds_stats != NULL) ? ds_stats : "",
			(sessions != NULL) ? sessions : "",
			(schemas != NULL) ? schemas : "",
			(stats != NULL) ? stats : "") == -1) {
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		retval = NULL;
	}
	if (retval == NULL) {
		retval = strdup("");
	}

	free(ds_stats);
	free(sessions);
	free(schemas);
	free(stats);

	return (retval);
}

char* get_state_nacm(const char* UNUSED(model), const char* UNUSED(running), struct nc_err ** UNUSED(e))
{
	char* retval = NULL;

	if (nc_info != NULL ) {
		pthread_rwlock_rdlock(&(nc_info->lock));
		if (asprintf(&retval, "<nacm xmlns=\"%s\">"
				"<denied-operations>%u</denied-operations>"
				"<denied-data-writes>%u</denied-data-writes>"
				"<denied-notifications>%u</denied-notifications>"
				"</nacm>",
				NC_NS_NACM,
				nc_info->stats_nacm.denied_ops,
				nc_info->stats_nacm.denied_data,
				nc_info->stats_nacm.denied_notifs) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			retval = NULL;
		}
		pthread_rwlock_unlock(&(nc_info->lock));
	}
	if (retval == NULL) {
		retval = strdup("");
	}

	return (retval);
}

static char* compare_schemas(struct data_model* model, char* name, char* version)
{
	char* retval = NULL;
	xmlBufferPtr resultbuffer;

	if (strcmp(name, model->name) == 0) {
		if (version == NULL || strcmp(version, model->version) == 0) {
			/* got the required model, dump it */
			resultbuffer = xmlBufferCreate();
			if (resultbuffer == NULL ) {
				ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
				return (ERROR_POINTER);
			}
			xmlNodeDump(resultbuffer, model->xml, model->xml->children, 2, 1);
			retval = strdup((char *) xmlBufferContent(resultbuffer));
			xmlBufferFree(resultbuffer);
		}
	}
	return (retval);
}

char* get_schema(const nc_rpc* rpc, struct nc_err** e)
{
	xmlXPathObjectPtr query_result = NULL;
	char *name = NULL, *version = NULL, *format = NULL;
	char *retval = NULL, *r = NULL;
	struct model_list* listitem;

	/* get name of the schema */
	if ((query_result = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/"NC_NS_MONITORING_ID":get-schema/"NC_NS_MONITORING_ID":identifier", rpc->ctxt)) != NULL &&
			!xmlXPathNodeSetIsEmpty(query_result->nodesetval)) {
		if (query_result->nodesetval->nodeNr > 1) {
			ERROR("%s: multiple identifier elements found", __func__);
			*e = nc_err_new(NC_ERR_BAD_ELEM);
			nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "identifier");
			nc_err_set(*e, NC_ERR_PARAM_MSG, "Multiple \'identifier\' elements found.");
			xmlXPathFreeObject(query_result);
			return (NULL);
		}
		name = (char*) xmlNodeGetContent(query_result->nodesetval->nodeTab[0]);
		xmlXPathFreeObject(query_result);
	} else {
		if (query_result != NULL) {
			xmlXPathFreeObject(query_result);
		}
		ERROR("%s: missing a mandatory identifier element", __func__);
		*e = nc_err_new(NC_ERR_INVALID_VALUE);
		nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "identifier");
		nc_err_set(*e, NC_ERR_PARAM_MSG, "Missing mandatory \'identifier\' element.");
		return (NULL);
	}

	/* get version of the schema */
	if ((query_result = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/"NC_NS_MONITORING_ID":get-schema/"NC_NS_MONITORING_ID":version", rpc->ctxt)) != NULL) {
		if (!xmlXPathNodeSetIsEmpty(query_result->nodesetval)) {
			if (query_result->nodesetval->nodeNr > 1) {
				ERROR("%s: multiple version elements found", __func__);
				*e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "version");
				nc_err_set(*e, NC_ERR_PARAM_MSG, "Multiple \'version\' elements found.");
				xmlXPathFreeObject(query_result);
				return (NULL);
			}
			version = (char*) xmlNodeGetContent(query_result->nodesetval->nodeTab[0]);
		}
		xmlXPathFreeObject(query_result);
	}

	/* get format of the schema */
	if ((query_result = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/"NC_NS_MONITORING_ID":get-schema/"NC_NS_MONITORING_ID":format", rpc->ctxt)) != NULL) {
		if (!xmlXPathNodeSetIsEmpty(query_result->nodesetval)) {
			if (query_result->nodesetval->nodeNr > 1) {
				ERROR("%s: multiple version elements found", __func__);
				*e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "version");
				nc_err_set(*e, NC_ERR_PARAM_MSG, "Multiple \'version\' elements found.");
				xmlXPathFreeObject(query_result);
				return (NULL);
			}
			format = (char*) xmlNodeGetContent(query_result->nodesetval->nodeTab[0]);

			/* only yin format is supported now */
			if (strcmp(format, "yin") != 0) {
				if (e != NULL) {
					*e = nc_err_new(NC_ERR_INVALID_VALUE);
					nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "format");
				}
				free(format);
				free(version);
				free(name);
				xmlXPathFreeObject(query_result);
				return(NULL);
			}

			/* only yin is supported now, so we do not use this parametes now */
			free(format);
			format = NULL;
		}
		xmlXPathFreeObject(query_result);
	}

	/* process all data models */
	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		r = compare_schemas(listitem->model, name, version);
		if (r == ERROR_POINTER) {
			free(version);
			free(name);
			if (e != NULL ) {
				*e = nc_err_new(NC_ERR_OP_FAILED);
			}
			return NULL ;
		} else if (r && retval) {
			/* schema is not unique according to request */
			free(r);
			free(retval);
			free(version);
			free(name);
			if (e != NULL ) {
				*e = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*e, NC_ERR_PARAM_APPTAG, "data-not-unique");
			}
			return (NULL );
		} else if (r != NULL ) {
			/* the first matching schema found */
			retval = r;
		}
	}

	if (retval == NULL) {
		*e = nc_err_new(NC_ERR_INVALID_VALUE);
		nc_err_set(*e, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(*e, NC_ERR_PARAM_MSG, "The requested schema does not exist.");
	}

	/* cleanup */
	free(version);
	free(name);

	return (retval);
}

struct ncds_ds* ncds_new_transapi(NCDS_TYPE type, const char* model_path, const char* callbacks_path)
{
	struct ncds_ds* ds = NULL;
	void * transapi_module = NULL;
	xmlDocPtr (*get_state)(const xmlDocPtr, const xmlDocPtr, struct nc_err **) = NULL;
	void (*close_func)(void) = NULL;
	int (*init_func)(xmlDocPtr *) = NULL;
	struct transapi_data_callbacks * data_clbks = NULL;
	struct transapi_rpc_callbacks * rpc_clbks = NULL;
	int *ver, ver_default = 1;
	int *modified;
	NC_EDIT_ERROPT_TYPE *erropt;
	char * ns_mapping = NULL;

	if (callbacks_path == NULL) {
		ERROR("%s: missing callbacks path parameter.", __func__);
		return (NULL);
	}

	/* load shared library */
	if ((transapi_module = dlopen (callbacks_path, RTLD_NOW)) == NULL) {
		ERROR("Unable to load shared library %s (%s).", callbacks_path, dlerror());
		return (NULL);
	}

	/* check transAPI version used to built the module */
	if ((ver = dlsym (transapi_module, "transapi_version")) == NULL) {
		WARN("transAPI version in module %s not found. Probably version 1, update your module.", callbacks_path);
		ver = &ver_default;
	}
	if (*ver != TRANSAPI_VERSION) {
		ERROR("Wrong transAPI version of the module %s. Have %d, but %d is required.", callbacks_path, *ver, TRANSAPI_VERSION);
		dlclose (transapi_module);
		return (NULL);
	}

	if ((modified = dlsym(transapi_module, "config_modified")) == NULL) {
		ERROR("Unable to get config_modified variable from shared library.");
		dlclose (transapi_module);
		return (NULL);
	}

	if ((erropt = dlsym(transapi_module, "erropt")) == NULL) {
		ERROR("Unable to get erropt variable from shared library.");
		dlclose (transapi_module);
		return (NULL);
	}

	/* find get_state function */
	if ((get_state = dlsym (transapi_module, "get_state_data")) == NULL) {
		ERROR("Unable to get addresses of functions from shared library.");
		dlclose (transapi_module);
		return (NULL);
	}

	if ((ns_mapping = dlsym(transapi_module, "namespace_mapping")) == NULL) {
		ERROR("Unable to get mapping of prefixes with uris.");
		dlclose(transapi_module);
		return(NULL);
	}

	/* find rpc callback functions mapping structure */
	if ((rpc_clbks = dlsym(transapi_module, "rpc_clbks")) == NULL) {
		WARN("Unable to get addresses of rpc callback functions from shared library.");
	}

	/* callbacks work with configuration data */
	/* empty datastore has no data */
	if (type != NCDS_TYPE_EMPTY) {
		/* get clbks structure */
		if ((data_clbks = dlsym (transapi_module, "clbks")) == NULL) {
			ERROR("Unable to get addresses of functions from shared library.");
			dlclose (transapi_module);
			return (NULL);
		}
	}

	if ((init_func = dlsym (transapi_module, "transapi_init")) == NULL) {
		WARN("%s: Unable to find \"transapi_init\" function.", __func__);
	}

	if ((close_func = dlsym (transapi_module, "transapi_close")) == NULL) {
		WARN("%s: Unable to find \"transapi_close\" function.", __func__);
	}

	/* create basic ncds_ds structure */
	if ((ds = ncds_new2(type, model_path, get_state)) == NULL) {
		ERROR ("Failed to create ncds_ds structure.");
		dlclose (transapi_module);
		return (NULL);
	}

	/* add pointers for transaction API */
	ds->transapi.module = transapi_module;
	ds->transapi.config_modified = modified;
	ds->transapi.erropt = erropt;
	ds->transapi.ns_mapping = (const char**)ns_mapping;
	ds->transapi.data_clbks = data_clbks;
	ds->transapi.rpc_clbks = rpc_clbks;
	ds->transapi.init = init_func;
	ds->transapi.close = close_func;

	return ds;
}

static struct data_model* data_model_new(const char* model_path)
{
	struct data_model *model = NULL;

	if (model_path == NULL ) {
		ERROR("%s: invalid parameter.", __func__);
		return (NULL);
	}

	/* get configuration data model */
	if (eaccess(model_path, R_OK) == -1) {
		ERROR("Unable to access the configuration data model %s (%s).", model_path, strerror(errno));
		return (NULL);
	}

	model = malloc(sizeof(struct data_model));
	if (model == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	model->xml = xmlReadFile(model_path, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (model->xml == NULL) {
		ERROR("Unable to read the configuration data model %s.", model_path);
		free(model);
		return (NULL);
	}

	/* prepare xpath evaluation context of the model for XPath */
	if ((model->ctxt = xmlXPathNewContext(model->xml)) == NULL) {
		ERROR("%s: Creating XPath context failed.", __func__);
		xmlFreeDoc(model->xml);
		free(model);
		return (NULL);
	}
	if (xmlXPathRegisterNs(model->ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model->ctxt);
		xmlFreeDoc(model->xml);
		free(model);
		return (NULL);
	}

	if (get_model_info(model->ctxt,
			&(model->name),
			&(model->version),
			&(model->namespace),
			&(model->prefix),
			&(model->rpcs),
			&(model->notifs)) != 0) {
		ERROR("Unable to process configuration data model %s.", model_path);
		xmlXPathFreeContext(model->ctxt);
		xmlFreeDoc(model->xml);
		free(model);
		return (NULL);
	}
	model->path = strdup(model_path);
	ncds_features_parse(model);
	model->model_tree = NULL;

	/* resolve uses statements in groupings and augments */
	ncds_update_uses_groupings(model);
	ncds_update_uses_augments(model);

	return (model);
}

static int data_model_enlink(struct data_model* model)
{
	struct model_list *listitem;

	if (model == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	/* check duplicity */
	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		if (listitem->model &&
		    strcmp(listitem->model->name, model->name) == 0 &&
		    strcmp(listitem->model->version, model->version) == 0) {
			/* module already found */
			ERROR("Module to enlink already exists.");
			return (EXIT_FAILURE);
		}
	}

	/* update internal model lists */
	listitem = malloc(sizeof(struct model_list));
	if (listitem == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (EXIT_FAILURE);
	}
	listitem->model = model;
	listitem->next = models_list;
	models_list = listitem;

	return (EXIT_SUCCESS);
}

static int match_module_node(char* path_module, char* module, char* name, xmlNodePtr *node)
{
	char* name_aux;

	if (path_module == NULL || module == NULL || name == NULL || node == NULL) {
		return (0);
	}

	if (strcmp(module, path_module) == 0) {
		/* we have the match - move into the specified element */
		while (*node != NULL ) {
			if (xmlStrcmp((*node)->name, BAD_CAST "container") == 0 ||
			    xmlStrcmp((*node)->name, BAD_CAST "list") == 0 ||
			    xmlStrcmp((*node)->name, BAD_CAST "choice") == 0 ||
			    xmlStrcmp((*node)->name, BAD_CAST "case") == 0 ||
			    xmlStrcmp((*node)->name, BAD_CAST "notification") == 0) {
				/* if the target is one of these, check its name attribute */
				if ((name_aux = (char*) xmlGetProp(*node, BAD_CAST "name")) == NULL ) {
					*node = (*node)->next;
					continue;
				}
				if (strcmp(name_aux, name) == 0) {
					free(name_aux);
					return (1);
				}
				free(name_aux);
				*node = (*node)->next;
			} else if (xmlStrcmp((*node)->name, BAD_CAST "input") == 0 ||
			    xmlStrcmp((*node)->name, BAD_CAST "output") == 0) {
				/* if the target is one of these, it has no name attribute, check directly its name (not attribute) */
				if (xmlStrcmp((*node)->name, BAD_CAST name) == 0) {
					return (1);
				}
				*node = (*node)->next;
			} else {
				/* target cannot be anything else (RFC 6020, sec. 7.15) */
				*node = (*node)->next;
			}
		}
	}

	return (0);
}

/* find the prefix in imports */
static char* get_module_with_prefix(const char* prefix, xmlXPathObjectPtr imports)
{
	int j;
	char *val, *module;
	xmlNodePtr node;

	if (prefix == NULL || imports == NULL) {
		return (NULL);
	}

	for (j = 0; j < imports->nodesetval->nodeNr; j++) {
		for (node = imports->nodesetval->nodeTab[j]->children; node != NULL ; node = node->next) {
			if (node->type == XML_ELEMENT_NODE && xmlStrcmp(node->name, BAD_CAST "prefix") == 0) {
				break;
			}
		}
		if (node != NULL ) {
			if ((val = (char*) xmlGetProp(node, BAD_CAST "value")) == NULL ) {
				continue;
			}
			if (strcmp(val, prefix) == 0) {
				free(val);
				if ((module = (char*) xmlGetProp(imports->nodesetval->nodeTab[j], BAD_CAST "module")) == NULL ) {
					continue;
				} else {
					/* we have the module name */
					return (module);
				}
			}
			free(val);
		}
	}

	return (NULL);
}

static struct data_model *read_model(const char* model_path)
{
	struct data_model *model;

	if (model_path == NULL) {
		ERROR("%s: invalid parameter model_path.", __func__);
		return (NULL);
	}

	/* get configuration data model information */
	if ((model = data_model_new(model_path)) == NULL) {
		return (NULL);
	}

	/* add a new model into the internal lists */
	if (data_model_enlink(model) != EXIT_SUCCESS) {
		ERROR("Adding new data model failed.");
		ncds_ds_model_free(model);
		return (NULL);
	}

	return (model);
}

/**
 * @param[in] datastore - datastore structure where the other data models will be imported
 * @param[in] model_ctxt - XPath context of the datastore's extended data model
 */
static int import_groupings(const char* module_name, xmlXPathContextPtr model_ctxt)
{
	xmlXPathObjectPtr imports, groupings;
	xmlNodePtr node, node_aux;
	xmlNsPtr ns;
	struct data_model* model;
	char *module, *revision, *prefix, *grouping_name, *aux;
	int i, j;

	aux = (char*) xmlGetNsProp(xmlDocGetRootElement(model_ctxt->doc), BAD_CAST "import", BAD_CAST "libnetconf");
	if (aux != NULL && strcmp(aux, "done") == 0) {
		/* import is already done by previous call */
		free(aux);
		return (EXIT_SUCCESS);
	}

	/* copy grouping definitions from imported models */
	if ((imports = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":import", model_ctxt)) == NULL ) {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}else if (!xmlXPathNodeSetIsEmpty(imports->nodesetval)) {
		/* we have something to import */
		for (i = 0; i < imports->nodesetval->nodeNr; i++) {
			revision = NULL;
			prefix = NULL;
			module = (char*) xmlGetProp(imports->nodesetval->nodeTab[i], BAD_CAST "module");
			if (module == NULL) {
				WARN("%s: invalid import statement - missing module reference.", __func__);
				continue;
			}
			for (node = imports->nodesetval->nodeTab[i]->children; node != NULL; node = node->next) {
				if (node->type != XML_ELEMENT_NODE ||
				    node->ns == NULL || node->ns->href == NULL ||
				    xmlStrcmp(node->ns->href, BAD_CAST NC_NS_YIN) != 0) {
					continue;
				}

				if (prefix == NULL && xmlStrcmp(node->name, BAD_CAST "prefix") == 0) {
					prefix = (char*) xmlGetProp(node, BAD_CAST "value");
				} else if (revision == NULL && xmlStrcmp(node->name, BAD_CAST "revision-date") == 0) {
					revision = (char*) xmlGetProp(node, BAD_CAST "value");
				}

				if (prefix != NULL && revision != NULL) {
					break;
				}
			}
			if (prefix == NULL) {
				ERROR("Invalid YIN module \'%s\' - missing prefix for imported \'%s\' module.", module_name, module);
				free(revision);
				free(module);
				return(EXIT_FAILURE);
			}
			model = get_model(module, revision);
			free(revision);
			if (model == NULL) {
				ERROR("Missing YIN module \'%s\' imported from \'%s\'.", module, module_name);
				free(module);
				free(prefix);
				xmlXPathFreeObject(imports);
				return(EXIT_FAILURE);
			}
			free(module);

			/* import grouping definitions */
			if ((groupings = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":grouping", model->ctxt)) != NULL ) {
				/* add prefix into the grouping names and add imported grouping into the overall data model */
				for (j = 0; j < groupings->nodesetval->nodeNr; j++) {
					node = xmlCopyNode(groupings->nodesetval->nodeTab[j], 1);
					grouping_name = (char*) xmlGetProp(node, BAD_CAST "name");
					asprintf(&aux, "%s:%s", prefix, grouping_name);
					xmlSetProp(node, BAD_CAST "name", BAD_CAST aux);
					free(aux);
					free(grouping_name);
					xmlAddChild(xmlDocGetRootElement(model_ctxt->doc), node);
				}
				free(prefix);
				xmlXPathFreeObject(groupings);
			} else {
				ERROR("%s: Evaluating XPath expression failed.", __func__);
				free(prefix);
				xmlXPathFreeObject(imports);
				return (EXIT_FAILURE);
			}
		}
		/* import done - note it */
		ns = xmlNewNs(xmlDocGetRootElement(model_ctxt->doc), BAD_CAST "libnetconf", BAD_CAST "libnetconf");
		xmlSetNsProp(xmlDocGetRootElement(model_ctxt->doc), ns, BAD_CAST "import", BAD_CAST "done");
	}
	xmlXPathFreeObject(imports);

	/* get all grouping statements in the document and remove unneeded nodes */
	if ((groupings = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":grouping", model_ctxt)) == NULL ) {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}
	if (!xmlXPathNodeSetIsEmpty(groupings->nodesetval)) {
		for (j = 0; j < groupings->nodesetval->nodeNr; j++) {
			/* remove unused data from the groupings */
			for (node = groupings->nodesetval->nodeTab[j]->children; node != NULL; ) {
				/*remember the next node */
				node_aux = node->next;

				if (node->type != XML_ELEMENT_NODE) {
					xmlUnlinkNode(node);
					xmlFreeNode(node);
				} else if (xmlStrcmp(node->name, BAD_CAST "description") == 0||
				    xmlStrcmp(node->name, BAD_CAST "grouping") == 0 || /* nested groupings are not supported */
				    xmlStrcmp(node->name, BAD_CAST "reference") == 0 ||
				    xmlStrcmp(node->name, BAD_CAST "status") == 0 ||
				    xmlStrcmp(node->name, BAD_CAST "typedef") == 0) {
					xmlUnlinkNode(node);
					xmlFreeNode(node);
				}

				/* process the next node */
				node = node_aux;
			}
		}
	}

	/* cleanup */
	xmlXPathFreeObject(groupings);

	return (EXIT_SUCCESS);
}

static int ncds_update_uses(const char* module_name, xmlXPathContextPtr *model_ctxt, const char* query)
{
	xmlXPathObjectPtr uses, groupings = NULL;
	xmlDocPtr doc;
	char *grouping_ref, *grouping_name;
	int i, j, flag = 0;

	if (model_ctxt == NULL || *model_ctxt == NULL || query == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}
	doc = (*model_ctxt)->doc;

	/*
	 * Get all uses statements from the data model
	 * Do this before import_groupings to get know if there is any uses
	 * clauses. If not, there is no need to import groupings and we can stop
	 * the processing
	 */
	if ((uses = xmlXPathEvalExpression(BAD_CAST query, *model_ctxt)) != NULL ) {
		if (xmlXPathNodeSetIsEmpty(uses->nodesetval)) {
			/* there is no <uses> part so no work is needed */
			xmlXPathFreeObject(uses);
			return (EXIT_SUCCESS);
		}
	} else {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}

	/* import grouping definitions from imported data models */
	if (import_groupings(module_name, *model_ctxt) != 0) {
		xmlXPathFreeObject(uses);
		return (EXIT_FAILURE);
	}

	if ((groupings = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":grouping", *model_ctxt)) == NULL ) {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}

	/* process all uses statements */
	while (!xmlXPathNodeSetIsEmpty(uses->nodesetval)) {
		for(i = 0; i < uses->nodesetval->nodeNr; i++) {
			grouping_ref = (char*) xmlGetProp(uses->nodesetval->nodeTab[i], BAD_CAST "name");
			flag = 0;
			/*
			 * we can process even references with prefix, because we
			 * already added imported groupings with changing their name
			 * to include prefix
			 */
			for (j = 0; j < groupings->nodesetval->nodeNr; j++) {
				grouping_name = (char*) xmlGetProp(groupings->nodesetval->nodeTab[j], BAD_CAST "name");
				if (strcmp(grouping_name, grouping_ref) == 0) {
					/* used grouping found */

					/* copy grouping content instead of uses statement */
					xmlAddChildList(uses->nodesetval->nodeTab[i]->parent, xmlCopyNodeList(groupings->nodesetval->nodeTab[j]->children));

					/* remove uses statement */
					xmlUnlinkNode(uses->nodesetval->nodeTab[i]);
					xmlFreeNode(uses->nodesetval->nodeTab[i]);
					uses->nodesetval->nodeTab[i] = NULL;

					free(grouping_name);
					flag = 1;
					break;
				}
				free(grouping_name);
			}
			free(grouping_ref);
		}

		if (flag != 0) {
			/*
			 * repeat uses processing - it could be in grouping, so we could
			 * add another not yet processed uses statement
			 */
			xmlXPathFreeObject(uses);

			/* remember to update model context */
			xmlXPathFreeContext(*model_ctxt);
			if ((*model_ctxt = xmlXPathNewContext(doc)) == NULL) {
				ERROR("%s: Creating XPath context failed.", __func__);
				return (EXIT_FAILURE);
			}
			if (xmlXPathRegisterNs(*model_ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
				xmlXPathFreeContext(*model_ctxt);
				return (EXIT_FAILURE);
			}

			if ((uses = xmlXPathEvalExpression(BAD_CAST query, *model_ctxt)) == NULL ) {
				ERROR("%s: Evaluating XPath expression failed.", __func__);
				return (EXIT_FAILURE);
			}
		} else {
			/* no change since last time */
			break;
		}
	}

	/* cleanup */
	xmlXPathFreeObject(groupings);
	xmlXPathFreeObject(uses);

	return (EXIT_SUCCESS);
}

static int ncds_update_uses_groupings(struct data_model* model)
{
	char* query;

	if (model == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	query = "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":grouping//"NC_NS_YIN_ID":uses";
	return(ncds_update_uses(model->name, &(model->ctxt), query));
}

static int ncds_update_uses_augments(struct data_model* model)
{
	char* query;

	if (model == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	query = "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":augment//"NC_NS_YIN_ID":uses";
	return(ncds_update_uses(model->name, &(model->ctxt), query));
}

static int ncds_update_uses_ds(struct ncds_ds* datastore)
{
	xmlXPathContextPtr model_ctxt;
	char* query;
	int ret;

	if (datastore == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	/*
	 * check that the extended model is already separated from the datastore's
	 * base model
	 */
	if (datastore->ext_model == datastore->data_model->xml) {
		datastore->ext_model = xmlCopyDoc(datastore->data_model->xml, 1);
	}

	/* prepare xpath evaluation context of the model for XPath */
	if ((model_ctxt = xmlXPathNewContext(datastore->ext_model)) == NULL) {
		ERROR("%s: Creating XPath context failed.", __func__);
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}

	query = "/"NC_NS_YIN_ID":module//"NC_NS_YIN_ID":uses";
	ret = ncds_update_uses(datastore->data_model->name, &model_ctxt, query);
	xmlXPathFreeContext(model_ctxt);

	return (ret);
}

/*
 *  1 - remove the node
 *  0 - do not remove the node
 * -1 - error
 */
static int feature_check(xmlNodePtr node, struct model_feature **features)
{
	xmlNodePtr child, next;
	char* fname;
	int i;

	if (node == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (-1);
	}
	if (features == NULL || features[0] == NULL) {
		/* nothing to check */
		return (0);
	}

	for (child = node->children; child != NULL; child = child->next) {
		if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, BAD_CAST "if-feature") == 0) {
			if ((fname = (char*) xmlGetProp (child, BAD_CAST "name")) == NULL) {
				WARN("Invalid if-feature statement");
				continue;
			}
			/* check if the feature is enabled or not */
			for (i = 0; features[i] != NULL; i++) {
				if (strcmp(features[i]->name, fname) == 0) {
					if (features[i]->enabled == 0) {
						free(fname);
						/* remove the node */
						return (1);
					}
					break;
				}
			}
			free(fname);
			/* ignore any following if-feature statements */
			break;
		}
	}

	/* recursion check */
	for (child = node->children; child != NULL; child = next) {
		next = child->next;
		if (feature_check(child, features) == 1) {
			/* remove the node */
			xmlUnlinkNode(child);
			xmlFreeNode(child);
		}
	}

	return (0);
}

static int ncds_update_features(struct ncds_ds* datastore)
{
	xmlNodePtr node, next;

	if (datastore == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	/*
	 * check that the extended model is already separated from the datastore's
	 * base model
	 */
	if (datastore->ext_model == datastore->data_model->xml) {
		datastore->ext_model = xmlCopyDoc(datastore->data_model->xml, 1);
	}

	for (node = xmlDocGetRootElement(datastore->ext_model)->children; node != NULL; node = next) {
		next = node->next;
		if (feature_check(node, datastore->data_model->features) == 1) {
			/* remove the node */
			xmlUnlinkNode(node);
			xmlFreeNode(node);
		}
	}

	return (EXIT_SUCCESS);
}

static int ncds_update_augment(struct data_model *augment)
{
	xmlXPathObjectPtr imports = NULL, augments = NULL;
	xmlNodePtr node, path_node;
	xmlNsPtr ns;
	int i, match;
	char *path, *token, *name, *prefix, *module, *module_inpath = NULL;
	struct ncds_ds* ds = NULL;
	struct ncds_ds_list *ds_iter;

	if (augment == NULL) {
		ERROR("%s: invalid parameter augment.", __func__);
		return (EXIT_FAILURE);
	}

	/* get all top-level's augment definitions */
	if ((augments = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":augment", augment->ctxt)) != NULL ) {
		if (xmlXPathNodeSetIsEmpty(augments->nodesetval)) {
			/* there is no <augment> part so it cannot be augment model and we have nothing to do */
			xmlXPathFreeObject(augments);
			return (EXIT_SUCCESS);
		}
	} else {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		goto error_cleanup;
	}

	/* get all <import> nodes for their prefix specification to be used with augment statement */
	if ((imports = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":import", augment->ctxt)) == NULL ) {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}

	/* process all augments from this model */
	for (i = 0; i < augments->nodesetval->nodeNr; i++) {

		/* get path to the augmented element */
		if ((path = (char*) xmlGetProp (augments->nodesetval->nodeTab[i], BAD_CAST "target-node")) == NULL) {
			ERROR("%s: Missing 'target-node' attribute in <augment>.", __func__);
			goto error_cleanup;
		}

		/* search for correct datastore in each augment according to its path */
		ds = NULL;

		/* path processing - check that we already have such an element */
		for (token = strtok(path, "/"); token != NULL; token = strtok(NULL, "/")) {
			if ((name = strchr(token, ':')) == NULL) {
				name = token;
				prefix = NULL;
			} else {
				name[0] = 0;
				name = &(name[1]);
				prefix = token;
			}

			if (ds == NULL) {
				/* locate corresponding datastore - we are at the beginning of the path */
				module = NULL;
				if (prefix == NULL) {
					/* model is augmenting itself - get the module's name to be able to find it */
					module = (char*) xmlGetProp(xmlDocGetRootElement(augment->xml), BAD_CAST "name");
				} else { /* (prefix != NULL) */
					/* find the prefix in imports */
					module = get_module_with_prefix(prefix, imports);
				}

				if (module == NULL) {
					/* unknown name of the module to augment */
					break;
				}

				/* locate the correct datastore to augment */
				for (ds_iter = ncds.datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
					if (ds_iter->datastore != NULL && strcmp(ds_iter->datastore->data_model->name, module) == 0) {
						ds = ds_iter->datastore;
						break;
					}
				}
				if (ds == NULL) {
					free(module);
					/* no such a datastore -> skip this augment */
					break; /* path processing */
				}

				if (ds->ext_model == ds->data_model->xml) {
					/* we have the first augment model */
					ds->ext_model = xmlCopyDoc(ds->data_model->xml, 1);
				}

				/* start path parsing with module root */
				path_node = ds->ext_model->children;

				module_inpath = strdup(ds->data_model->name);
			} else {
				/* we are somewhere in the path and we need to connect declared prefix with the corresponding module */

				if (prefix == NULL) {
					prefix = augment->prefix;
				}

				if (strcmp(prefix, augment->prefix) == 0) {
					/* the path is augmenting the self module */
					module = strdup(augment->name);
				} else {
					/* find the prefix in imports */
					module = get_module_with_prefix(prefix, imports);
				}
				if (module == NULL ) {
					/* unknown name of the module to augment */
					break;
				}
			}

			match = 0;
			path_node = path_node->children;
			if (module_inpath != NULL && strcmp(module, module_inpath) != 0) {
				/* the prefix is changing, so there must be an augment element */
				for (node = path_node; node != NULL && match == 0; node = node->next) {
					if (xmlStrcmp(node->name, BAD_CAST "augment") != 0) {
						continue;
					}
					/* some augment found, now check it */
					free(module_inpath);
					module_inpath = (char*) xmlGetNsProp(path_node, BAD_CAST "module", BAD_CAST "libnetconf");

					path_node = node->children;
					match = match_module_node(module_inpath, module, name, &path_node);
				}
			} else if (module_inpath != NULL && strcmp(module, module_inpath) == 0) {
				/* we have the match - move into the specified element */
				match = match_module_node(module_inpath, module, name, &path_node);
			}
			free(module);

			if (match == 0) {
				/* we didn't find the matching path */
				break;
			}
		}
		if (token == NULL) {
			/* whole path is correct - add the subtree */
			xmlAddChild(path_node, node = xmlCopyNode(augments->nodesetval->nodeTab[i], 1));
			ns = xmlNewNs(node, BAD_CAST "libnetconf", BAD_CAST "libnetconf");
			xmlSetNsProp(node, ns, BAD_CAST "module", BAD_CAST augment->name);
			xmlSetNsProp(node, ns, BAD_CAST "ns", BAD_CAST augment->namespace);
		}

		free(module_inpath);
		module_inpath = NULL;
		free(path);
	}
	xmlXPathFreeObject(augments);
	xmlXPathFreeObject(imports);

	return (EXIT_SUCCESS);

error_cleanup:

	if (imports) {xmlXPathFreeObject(imports);}
	if (augments) {xmlXPathFreeObject(augments);}
	return (EXIT_FAILURE);
}

int ncds_add_models_path(const char* path)
{
	static int list_size = 0;
	static int list_records = 0;

	if (models_dirs == NULL) {
		/* the list was cleaned */
		list_size = 0;
		list_records = 0;
	}

	if (path == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	if (access(path, R_OK | X_OK) != 0) {
		ERROR("Configuration data models directory \'%s\' is not accessible (%s).", path, strerror(errno));
		return (EXIT_FAILURE);
	}

	if (list_records + 1 >= list_size) {
		void *tmp = realloc(models_dirs, (list_size + 5) * sizeof(char*));
		if (tmp == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}

		models_dirs = tmp;
		list_size += 5;
	}

	models_dirs[list_records] = strdup(path);
	if (models_dirs[list_records] == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (EXIT_FAILURE);
	}

	list_records++;
	models_dirs[list_records] = NULL; /* terminating NULL byte */

	return (EXIT_SUCCESS);
}

int ncds_add_model(const char* model_path)
{
	if (model_path == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	if (read_model(model_path) == NULL) {
		return (EXIT_FAILURE);
	} else {
		return (EXIT_SUCCESS);
	}
}

static struct data_model* get_model(const char* module, const char* version)
{
	struct model_list* listitem;
	struct data_model* model = NULL;
	int i;
	char* aux, *aux2;
	DIR* dir;
	struct dirent* file;

	if (module == NULL) {
		return (NULL);
	}

	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		if (listitem->model && strcmp(listitem->model->name, module) == 0) {
			if (version != NULL) {
				if (strcmp(listitem->model->version, version) == 0) {
					/* module found */
					return (listitem->model);
				} else {
					/* module version does not match */
					continue;
				}
			} else {
				/* module found - specific version is not required */
				return (listitem->model);
			}
		}
	}

	/* module not found - try to find it in a models directories */
	if (models_dirs != NULL) {
		for (i = 0; models_dirs[i]; i++) {
			aux = NULL;
			asprintf(&aux, "%s/%s.yin", models_dirs[i], module);
			if (access(aux, R_OK) == 0) {
				/* we have found the correct module - probably */
				model = read_model(aux);
				if (model != NULL) {
					if (strcmp(model->name, module) != 0) {
						/* read module is incorrect */
						ncds_ds_model_free(model);
						model = NULL;
					}
				}
			} else {
				/*
				 * filename can contain also revision, so try to open
				 * all suitable files
				 */
				free(aux);
				if (version == NULL) {
					asprintf(&aux, "%s@", module);
				} else {
					asprintf(&aux, "%s@%s", module, version);
				}
				dir = opendir(models_dirs[i]);
				while((file = readdir(dir)) != NULL) {
					if (strncmp(file->d_name, aux, strlen(aux)) == 0 &&
					    strcmp(&(file->d_name[strlen(file->d_name)-4]), ".yin") == 0) {
						asprintf(&aux2, "%s/%s", models_dirs[i], file->d_name);
						model = read_model(aux2);
						free(aux2);
						if (model != NULL) {
							if (strcmp(model->name, module) != 0) {
								/* read module is incorrect */
								ncds_ds_model_free(model);
								model = NULL;
							}
						}
					}
				}
				closedir(dir);
			}
			free(aux);
			if (model != NULL) {
				return (model);
			}
		}
	}

	return (model);
}

static int ncds_features_parse(struct data_model* model)
{
	xmlXPathObjectPtr features = NULL;
	int i;

	if (model == NULL || model->ctxt == NULL) {
		ERROR("%s: invalid parameter.", __func__);
		return (EXIT_FAILURE);
	}

	/* get all top-level's augment definitions */
	if ((features = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":feature", model->ctxt)) != NULL ) {
		if (xmlXPathNodeSetIsEmpty(features->nodesetval)) {
			/* there is no <feature> part so feature list will be empty */
			model->features = NULL;

			VERB("%s: no feature definitions found in data model %s.", __func__, model->name);
			xmlXPathFreeObject(features);
			return (EXIT_SUCCESS);
		}

		model->features = malloc((features->nodesetval->nodeNr + 1) * sizeof(struct model_feature*));
		if (model->features == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			xmlXPathFreeObject(features);
			return (EXIT_FAILURE);
		}
		for(i = 0; i < features->nodesetval->nodeNr; i++) {
			model->features[i] = malloc(sizeof(struct model_feature));
			if (model->features[i] == NULL) {
				ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
				xmlXPathFreeObject(features);
				return (EXIT_FAILURE);
			}
			if ((model->features[i]->name = (char*) xmlGetProp(features->nodesetval->nodeTab[i], BAD_CAST "name")) == NULL ) {
				ERROR("xmlGetProp failed (%s:%d).", __FILE__, __LINE__);
				free(model->features[i]);
				model->features = NULL;
				xmlXPathFreeObject(features);
				return (EXIT_FAILURE);
			}
			/* by default, all features are disabled */
			model->features[i]->enabled = false;
		}
		model->features[i] = NULL; /* list terminating NULL byte */

		xmlXPathFreeObject(features);
	} else {
		ERROR("%s: Evaluating XPath expression failed.", __func__);
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

int ncds_feature_isenabled(const char* module, const char* feature)
{
	struct data_model* model;
	int i;

	if (module == NULL || feature == NULL) {
		ERROR("%s: invalid parameter %s", __func__, (module==NULL)?"module":"feature");
		return (-1);
	}

	if ((model = get_model(module, NULL)) == NULL) {
		return (-1);
	}

	if (model->features != NULL) {
		for (i = 0; model->features[i] != NULL; i++) {
			if (strcmp(model->features[i]->name, feature) == 0) {
				return(model->features[i]->enabled);
			}
		}
	}
	return (-1);
}

static inline int _feature_switch(const char* module, const char* feature, int value)
{
	struct data_model* model;
	int i;

	if (module == NULL || feature == NULL) {
		ERROR("%s: invalid parameter %s", __func__, (module==NULL)?"module":"feature");
		return (EXIT_FAILURE);
	}

	if ((model = get_model(module, NULL)) == NULL) {
		return (EXIT_FAILURE);
	}

	if (model->features != NULL) {
		for (i = 0; model->features[i] != NULL; i++) {
			if (strcmp(model->features[i]->name, feature) == 0) {
				model->features[i]->enabled = value;
				return (EXIT_SUCCESS);
			}
		}
	}

	return (EXIT_FAILURE);
}

int ncds_feature_enable(const char* module, const char* feature)
{
	return (_feature_switch(module, feature, 1));
}

int ncds_feature_disable(const char* module, const char* feature)
{
	return (_feature_switch(module, feature, 0));
}

static inline int _features_switchall(const char* module, int value)
{
	struct data_model* model;
	int i;

	if (module == NULL) {
		ERROR("%s: invalid parameter", __func__);
		return (EXIT_FAILURE);
	}

	if ((model = get_model(module, NULL)) == NULL) {
		return (EXIT_FAILURE);
	}

	if (model->features != NULL) {
		for (i = 0; model->features[i] != NULL; i++) {
			model->features[i]->enabled = value;
		}
	}

	return (EXIT_SUCCESS);
}

int ncds_features_enableall(const char* module)
{
	return (_features_switchall(module, 1));
}

int ncds_features_disableall(const char* module)
{
	return (_features_switchall(module, 1));
}

int ncds_consolidate(void)
{
	struct ncds_ds_list *ds_iter;
	struct model_list* listitem;

	/* process uses statements in the configuration datastores */
	for (ds_iter = ncds.datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ncds_update_uses_ds(ds_iter->datastore) != EXIT_SUCCESS) {
			ERROR("Preparing configuration data models failed.");
			return (EXIT_FAILURE);
		}
	}

	/* augment statement processing */
	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		if (listitem->model != NULL && ncds_update_augment(listitem->model) != EXIT_SUCCESS) {
			ERROR("Augmenting configuration data models failed.");
			return (EXIT_FAILURE);
		}
	}

	for (ds_iter = ncds.datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		/* remove disabled feature subtrees */
		ncds_update_features(ds_iter->datastore);
	}

	/* parse models to get aux structure for TransAPI's internal purposes */
	for (ds_iter = ncds.datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		/* when using transapi */
		if (ds_iter->datastore->transapi.module != NULL) {
			/* parse only models not parsed yet */
			if (ds_iter->datastore->data_model->model_tree == NULL) {
				if ((ds_iter->datastore->data_model->model_tree = yinmodel_parse(ds_iter->datastore->ext_model, ds_iter->datastore->transapi.ns_mapping)) == NULL) {
					WARN("Failed to parse model %s. Callbacks of transAPI modules using this model will not be executed.", ds_iter->datastore->data_model->name)
				}
			}
		}
	}

	return (EXIT_SUCCESS);
}

/**
 * \brief Get an appropriate root node from edit-config's \<config\> element according to the specified data model
 *
 * \param[in] roots First of the root elements in edit-config's \<config\>
 *                  (first children of this element).
 * \param[in] model XML form (YIN) of the configuration data model.
 *
 * \return Root element matching the specified configuration data model.
 */
static xmlNodePtr get_model_root(xmlNodePtr roots, struct data_model *data_model)
{
	xmlNodePtr retval;

	assert(roots != NULL);
	assert(data_model != NULL);

	if (data_model == NULL) {
		ERROR("%s: Invalid argument - data model is unknown.", __func__);
		return NULL;
	}
	if (data_model->namespace == NULL) {
		ERROR("Invalid configuration data model '%s'- namespace is missing.", data_model->name);
		return NULL;
	}

	retval = roots;
	while (retval != NULL) {
		if (retval->ns == NULL || xmlStrcmp(retval->ns->href, BAD_CAST (data_model->namespace)) == 0) {
			break;
		}

		retval = retval->next;
	}

	return retval;
}

#ifndef DISABLE_VALIDATION
static void relaxng_error_callback(void *error, const char * msg, ...)
{
	struct nc_err **e = (struct nc_err**)(error);
	va_list args;
	char* s = NULL, *m = NULL;

	if (e != NULL && *e == NULL) {
		/*
		 * if the callback is invoked multiply, only the first error message
		 * will be stored as NETCONF error
		 */

		va_start(args, msg);
		vasprintf(&s, msg, args);
		va_end(args);

		asprintf(&m, "Datastore fails to validate (%s)", s);
		*e = nc_err_new(NC_ERR_OP_FAILED);
		nc_err_set(*e, NC_ERR_PARAM_MSG, m);

		free(s);
		free(m);
	}
}

/*
 * EXIT_SUCCESS - validation ok
 * EXIT_FAILURE - validation failed
 * EXIT_RPC_NOT_APPLICABLE - RelaxNG scheme not defined
 */
static int validate_ds(struct ncds_ds *ds, xmlDocPtr doc, struct nc_err **error)
{
	int ret = 0;
	int retval = EXIT_RPC_NOT_APPLICABLE;
	xmlDocPtr sch_result;
	xmlXPathContextPtr ctxt = NULL;
	xmlXPathObjectPtr result = NULL;
	char* schematron_error = NULL, *error_string = NULL;

	assert(error != NULL);

	if (ds == NULL || doc == NULL) {
		ERROR("%s: invalid input parameter", __func__);
		return (EXIT_FAILURE);
	}

	if (ds->validators.rng) {
		/* RelaxNG validation */
		DBG("RelaxNG validation on subdatastore %d", ds->id);

		xmlRelaxNGSetValidErrors(ds->validators.rng,
			(xmlRelaxNGValidityErrorFunc) relaxng_error_callback,
			(xmlRelaxNGValidityWarningFunc) relaxng_error_callback,
			error);

		ret = xmlRelaxNGValidateDoc(ds->validators.rng, doc);
		if (ret > 0) {
			VERB("subdatastore %d fails to validate", ds->id);
			if (*error == NULL) {
				*error = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*error, NC_ERR_PARAM_MSG, "Datastore fails to validate.");
			}
			return (EXIT_FAILURE);
		} else if (ret < 0) {
			ERROR("validation generated an internal error", ds->id);
			if (*error == NULL) {
				*error = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*error, NC_ERR_PARAM_MSG, "Validation generated an internal error.");
			}
			return (EXIT_FAILURE);
		} else { /* ret == 0 -> success */
			retval = EXIT_SUCCESS;
		}
	}

	if (ds->validators.schematron) {
		/* schematron */
		DBG("Schematron validation on subdatastore %d", ds->id);

		sch_result = xsltApplyStylesheet(ds->validators.schematron, doc, NULL);
		if (sch_result == NULL) {
			ERROR("Applying Schematron stylesheet on subdatastore %d failed", ds->id);
			*error = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(*error, NC_ERR_PARAM_MSG, "Schematron validation internal error.");
			return (EXIT_FAILURE);
		}

		/* parse SVRL result */
		if ((ctxt = xmlXPathNewContext(sch_result)) == NULL) {
			ERROR("%s: Creating the XPath context failed.", __func__);
			xmlFreeDoc(sch_result);
			*error = nc_err_new(NC_ERR_OP_FAILED);
			return (EXIT_FAILURE);
		}
		if (xmlXPathRegisterNs(ctxt, BAD_CAST "svrl", BAD_CAST "http://purl.oclc.org/dsdl/svrl") != 0) {
			ERROR("Registering SVRL namespace for the xpath context failed.");
			xmlXPathFreeContext(ctxt);
			xmlFreeDoc(sch_result);
			*error = nc_err_new(NC_ERR_OP_FAILED);
			return (EXIT_FAILURE);
		}
		if ((result = xmlXPathEvalExpression(BAD_CAST "/svrl:schematron-output/svrl:successful-report/svrl:text", ctxt)) != NULL) {
			if (!xmlXPathNodeSetIsEmpty(result->nodesetval)) {
				schematron_error = (char*)xmlNodeGetContent(result->nodesetval->nodeTab[0]);
				asprintf(&error_string, "Datastore fails to validate: %s", schematron_error);
				ERROR(error_string);
				*error = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*error, NC_ERR_PARAM_MSG, error_string);
				free(error_string);
				free(schematron_error);

				xmlXPathFreeObject(result);
				xmlXPathFreeContext(ctxt);
				xmlFreeDoc(sch_result);

				return (EXIT_FAILURE);
			} else {
				/* there is no error */
				retval = EXIT_SUCCESS;
			}
			xmlXPathFreeObject(result);
		} else {
			WARN("Evaluating Schematron output failed");
		}
		xmlXPathFreeContext(ctxt);
		xmlFreeDoc(sch_result);
	}

	return (retval);
}

static int apply_rpc_validate_(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE source, const char* config, struct nc_err** e)
{
	int ret = EXIT_FAILURE;
	int len;
	char *data_cfg = NULL, *data2, *model, *config_internal;
	xmlDocPtr doc_cfg, doc_status, doc = NULL;
	xmlNodePtr root;
	xmlNsPtr ns;
	xmlBufferPtr resultbuffer;

	if (!ds->validators.rng && !ds->validators.rng_schema && !ds->validators.schematron) {
		/* validation not supported by this datastore */
		return (EXIT_RPC_NOT_APPLICABLE);
	}

	/* init/clean error information */
	*e = NULL;

	switch (source) {
	case NC_DATASTORE_RUNNING:
	case NC_DATASTORE_STARTUP:
	case NC_DATASTORE_CANDIDATE:
		if ((data_cfg = ds->func.getconfig(ds, session, source, e)) == NULL ) {
			if (*e == NULL ) {
				ERROR("%s: Failed to get data from the datastore (%s:%d).", __func__, __FILE__, __LINE__);
				*e = nc_err_new(NC_ERR_OP_FAILED);
			}
			return (EXIT_FAILURE);
		}
		doc = doc_cfg = xmlReadDoc(BAD_CAST data_cfg, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		/* if the datastore is empty, doc1/aux_doc is NULL here */

		if (ds->get_state != NULL ) {
			xmlDocDumpMemory(ds->ext_model, (xmlChar**) (&model), &len);
			data2 = ds->get_state(model, data_cfg, e);
			free(model);

			if (*e != NULL ) {
				/* state data retrieval error */
				free(data2);
				free(data_cfg);
				return (EXIT_FAILURE);
			}

			doc_status = xmlReadDoc(BAD_CAST data2, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			free(data2);

			if (data_cfg == NULL || strisempty(data_cfg)) {
				/* there are no configuration data, use only status, no merge is needed */
				doc = doc_status;
				/* doc_cfg is NULL and don't need to free() */
			} else if ((doc = ncxml_merge(doc_cfg, doc_status, ds->ext_model)) == NULL ) {
				xmlFreeDoc(doc_cfg);
				xmlFreeDoc(doc_status);
				free(data_cfg);
				*e = nc_err_new(NC_ERR_OP_FAILED);
				return (EXIT_FAILURE);
			} else {
				/* cleanup */
				xmlFreeDoc(doc_cfg);
				xmlFreeDoc(doc_status);
			}
		}
		free(data_cfg);
		data_cfg = NULL;
		break;
	case NC_DATASTORE_CONFIG:
		/*
		 * config can contain multiple elements on the root level, so
		 * cover it with the <config> element to allow the creation of xml
		 * document
		 */
		if (config == NULL || strcmp(config, "") == 0) {
			/* config is empty -> ignore rest of magic here,
			 * go to validation
			 */
			doc = xmlNewDoc(BAD_CAST "1.0");
			break;
		}

		if (asprintf(&data_cfg, "<config>%s</config>", config) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			*e = nc_err_new(NC_ERR_OP_FAILED);
			return (EXIT_FAILURE);
		}
		doc_cfg = xmlReadDoc(BAD_CAST data_cfg, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		free(data_cfg);
		data_cfg = NULL;

		if (doc_cfg == NULL || doc_cfg->children == NULL || doc_cfg->children->children == NULL) {
			xmlFreeDoc(doc_cfg);
			*e = nc_err_new(NC_ERR_INVALID_VALUE);
			nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid <config> parameter of the rpc request.");
			break;
		}

		/*
		 * select correct config node for the selected datastore,
		 * it must match the model's namespace and root element name
		 */
		root = get_model_root(doc_cfg->children->children, ds->data_model);
		if (root != NULL) {
			resultbuffer = xmlBufferCreate();
			if (resultbuffer == NULL) {
				ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
				*e = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*e, NC_ERR_PARAM_MSG, "Internal error, see libnetconf error log.");
				break;
			}
			xmlNodeDump(resultbuffer, doc_cfg, root, 2, 1);
			if ((config_internal = strdup((char*) xmlBufferContent(resultbuffer))) == NULL) {
				xmlBufferFree(resultbuffer);
				xmlFreeDoc(doc_cfg);
				ERROR("%s: xmlBufferContent failed (%s:%d)", __func__, __FILE__, __LINE__);
				*e = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*e, NC_ERR_PARAM_MSG, "Internal error, see libnetconf error log.");
				break;
			}
			/*
			 * now we have config as a valid xml tree with the
			 * single root
			 */
			xmlBufferFree(resultbuffer);
			xmlFreeDoc(doc_cfg);

			doc = xmlReadDoc(BAD_CAST config_internal, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			free(config_internal);
			config_internal = NULL;
		} else {
			xmlFreeDoc(doc_cfg);
			/* request is not intended for this device */
			return (EXIT_RPC_NOT_APPLICABLE);
		}
		break;
	default:
		*e = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "source");
		return (EXIT_FAILURE);
	}

	if (doc == NULL){
		if (*e != NULL) {
			/* some error occurred */
			ret = EXIT_FAILURE;
		} else {
			/*
			 * there are no data to validate - datastore is empty and
			 * it is a valid state of the datastore
			 */
			ret = EXIT_SUCCESS;
		}
	} else {
		/* process default values */
		ncdflt_default_values(doc, ds->ext_model, NCWD_MODE_ALL);

		/*
		 * reconnect root element from datastore data under the <data>
		 * element required by validators
		 */
		root = xmlDocGetRootElement(doc);
		xmlUnlinkNode(root);
		xmlDocSetRootElement(doc, xmlNewDocNode(doc, NULL, BAD_CAST "data", NULL ));
		ns = xmlNewNs(doc->children, (xmlChar *) NC_NS_BASE10, NULL );
		xmlSetNs(doc->children, ns);
		xmlAddChild(doc->children, root);

		ret = validate_ds(ds, doc, e);

		xmlFreeDoc(doc);
	}

	return (ret);

}

static int apply_rpc_validate(struct ncds_ds* ds, const struct nc_session* session, const nc_rpc* rpc, struct nc_err** e)
{
	int ret;
	char *config;
	NC_DATASTORE source;

	if (!ds->validators.rng && !ds->validators.rng_schema && !ds->validators.schematron) {
		/* validation not supported by this datastore */
		return (EXIT_RPC_NOT_APPLICABLE);
	}

	switch (source = nc_rpc_get_source(rpc)) {
	case NC_DATASTORE_RUNNING:
	case NC_DATASTORE_STARTUP:
	case NC_DATASTORE_CANDIDATE:
		ret = apply_rpc_validate_(ds, session, source, NULL, e);
		break;
	case NC_DATASTORE_URL:
	case NC_DATASTORE_CONFIG:
		/*
		 * config can contain multiple elements on the root level, so
		 * cover it with the <config> element to allow the creation of xml
		 * document
		 */
		config = nc_rpc_get_config(rpc);
		ret = apply_rpc_validate_(ds, session, NC_DATASTORE_CONFIG, NULL, e);
		free(config);
		break;
	default:
		*e = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "source");
		ret = EXIT_FAILURE;
		break;
	}

	return (ret);
}

#endif /* not DISABLE_VALIDATION */

int ncds_set_validation(struct ncds_ds* ds, int enable, const char* relaxng, const char* schematron)
{
#ifdef DISABLE_VALIDATION
	return (EXIT_SUCCESS);
#else
	int ret = EXIT_SUCCESS;
	xmlRelaxNGParserCtxtPtr rng_ctxt = NULL;
	xmlRelaxNGPtr rng_schema = NULL;
	xmlRelaxNGValidCtxtPtr rng = NULL;
	xsltStylesheetPtr schxsl = NULL;

	if (enable == 0) {
		/* disable validation on this datastore */
		xmlRelaxNGFreeValidCtxt(ds->validators.rng);
		xmlRelaxNGFree(ds->validators.rng_schema);
		xsltFreeStylesheet(ds->validators.schematron);
		memset(&(ds->validators), 0, sizeof(struct model_validators));
	} else if (nc_init_flags & NC_INIT_VALIDATE) { /* && enable == 1 */
		/* enable and reset validators */
		if (relaxng != NULL) {
			/* prepare validators - Relax NG */
			if (eaccess(relaxng, R_OK) == -1) {
				ERROR("%s: Unable to access RelaxNG schema for validation (%s - %s).", __func__, relaxng, strerror(errno));
				ret = EXIT_FAILURE;
				goto cleanup;
			} else {
				rng_ctxt = xmlRelaxNGNewParserCtxt(relaxng);
				if ((rng_schema = xmlRelaxNGParse(rng_ctxt)) == NULL) {
					ERROR("Failed to parse Relax NG schema (%s)", relaxng);
					ret = EXIT_FAILURE;
					goto cleanup;
				} else if ((rng = xmlRelaxNGNewValidCtxt(rng_schema)) == NULL) {
					ERROR("Failed to create validation context (%s)", relaxng);
					ret = EXIT_FAILURE;
					goto cleanup;
				}
				xmlRelaxNGFreeParserCtxt(rng_ctxt);
				rng_ctxt = NULL;
			}
		}

		if (schematron != NULL) {
			/* prepare validators - Schematron */
			if (eaccess(schematron, R_OK) == -1) {
				ERROR("%s: Unable to access Schematron stylesheet for validation (%s - %s).", __func__, schematron, strerror(errno));
				ret = EXIT_FAILURE;
				goto cleanup;
			} else {
				if ((schxsl = xsltParseStylesheetFile(BAD_CAST schematron)) == NULL) {
					ERROR("Failed to parse Schematron stylesheet (%s)", schematron);
					ret = EXIT_FAILURE;
					goto cleanup;
				}
			}
		}

		/* replace previous validators */
		if (rng_schema && rng) {
			xmlRelaxNGFree(ds->validators.rng_schema);
			ds->validators.rng_schema = rng_schema;
			rng_schema = NULL;
			xmlRelaxNGFreeValidCtxt(ds->validators.rng);
			ds->validators.rng = rng;
			rng = NULL;
			DBG("%s: Relax NG validator set (%s)", __func__, relaxng);
		}
		if (schxsl) {
			xsltFreeStylesheet(ds->validators.schematron);
			ds->validators.schematron = schxsl;
			schxsl = NULL;
			DBG("%s: Schematron validator set (%s)", __func__, schematron);
		}

	}

cleanup:
	xmlRelaxNGFreeValidCtxt(rng);
	xmlRelaxNGFree(rng_schema);
	xmlRelaxNGFreeParserCtxt(rng_ctxt);
	xsltFreeStylesheet(schxsl);

	return (ret);
#endif
}

struct ncds_ds* ncds_new_internal(NCDS_TYPE type, const char * model_path)
{
	struct ncds_ds* ds = NULL;
	char *basename, *path_yin;

#ifndef DISABLE_VALIDATION
	char *path_rng = NULL, *path_sch = NULL;
	xmlRelaxNGParserCtxtPtr rng_ctxt;
#endif

	if (model_path == NULL) {
		ERROR("%s: missing the model path parameter.", __func__);
		return (NULL);
	}

	/* check the form of model_path - for backward compatibility
	 * model_path.yin is supported and validation file names will be also
	 * derived
	 */
	basename = strdup(model_path);
	if (strcmp(&(basename[strlen(basename)-4]), ".yin") == 0) {
		path_yin = strdup(model_path);
		basename[strlen(basename)-4] = 0; /* remove .yin suffix */
	} else {
		asprintf(&path_yin, "%s.yin", basename);
	}
#ifndef DISABLE_VALIDATION
	asprintf(&path_rng, "%s-data.rng", basename);
	asprintf(&path_sch, "%s-schematron.xsl", basename);
#endif

	ds = ncds_fill_func(type);
	if (ds == NULL) {
		/* The error was reported already. */
		goto cleanup;
	}

	ds->type = type;

	/* get configuration data model */
	ds->data_model = read_model(path_yin);
	if (ds->data_model == NULL) {
		free(ds);
		ds = NULL;
		goto cleanup;
	}
	ds->ext_model = ds->data_model->xml;

#ifndef DISABLE_VALIDATION
	if (nc_init_flags & NC_INIT_VALIDATE) {
		/* prepare validation - Relax NG */
		if (eaccess(path_rng, R_OK) == -1) {
			WARN("Missing RelaxNG schema for validation (%s - %s).", path_rng, strerror(errno));
		} else {
			rng_ctxt = xmlRelaxNGNewParserCtxt(path_rng);
			if ((ds->validators.rng_schema = xmlRelaxNGParse(rng_ctxt)) == NULL) {
				WARN("Failed to parse Relax NG schema (%s)", path_rng);
			} else if ((ds->validators.rng = xmlRelaxNGNewValidCtxt(ds->validators.rng_schema)) == NULL) {
				WARN("Failed to create validation context (%s)", path_rng);
				xmlRelaxNGFree(ds->validators.rng_schema);
				ds->validators.rng_schema = NULL;
			} else {
				DBG("%s: Relax NG validator set (%s)", __func__, path_rng);
			}
			xmlRelaxNGFreeParserCtxt(rng_ctxt);
		}

		/* prepare validation - Schematron */
		if (eaccess(path_sch, R_OK) == -1) {
			WARN("Missing Schematron stylesheet for validation (%s - %s).", path_sch, strerror(errno));
		} else {
			if ((ds->validators.schematron = xsltParseStylesheetFile(BAD_CAST path_sch)) == NULL) {
				WARN("Failed to parse Schematron stylesheet (%s)", path_sch);
			} else {
				DBG("%s: Schematron validator set (%s)", __func__, path_sch);
			}
		}
	}
#endif /* not DISABLE_VALIDATION */

	/* TransAPI structure is set to NULLs */

	ds->last_access = 0;

	/* ds->id is -1 to indicate, that datastore is still not fully configured */
	ds->id = -1;

cleanup:
	free(basename);
	free(path_yin);

#ifndef DISABLE_VALIDATION
	free(path_rng);
	free(path_sch);
#endif

	return (ds);
}

struct ncds_ds* ncds_new2(NCDS_TYPE type, const char * model_path, xmlDocPtr (*get_state)(const xmlDocPtr model, const xmlDocPtr running, struct nc_err **e))
{
	struct ncds_ds * ds;

	if ((ds = ncds_new_internal(type, model_path)) != NULL) {
		ds->get_state_xml = get_state;
		ds->get_state = NULL;
	}

	return(ds);
}

struct ncds_ds* ncds_new(NCDS_TYPE type, const char* model_path, char* (*get_state)(const char* model, const char* running, struct nc_err** e))
{
	struct ncds_ds * ds;

	if ((ds = ncds_new_internal(type, model_path)) != NULL) {
		ds->get_state_xml = NULL;
		ds->get_state = get_state;
	}

	return(ds);
}

ncds_id generate_id(void)
{
	ncds_id current_id;

	do {
		/* generate id */
		current_id = (rand() + 1) % INT_MAX;
		/* until it's unique */
	} while (datastores_get_ds(current_id) != NULL);

	return current_id;
}

void ncds_ds_model_free(struct data_model* model)
{
	int i;
	struct model_list *listitem, *listprev;

	if (model == NULL) {
		return;
	}

	/* models_list */
	listprev = NULL;
	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		if (listitem->model == model) {
			if (listprev != NULL) {
				listprev->next = listitem->next;
			} else {
				models_list = listitem->next;
			}
			free(listitem);
			break;
		}
		listprev = listitem;
	}

	free(model->path);
	free(model->name);
	free(model->version);
	free(model->namespace);
	free(model->prefix);
	if (model->rpcs != NULL) {
		for (i = 0; model->rpcs[i] != NULL; i++) {
			free(model->rpcs[i]);
		}
		free(model->rpcs);
	}
	if (model->notifs != NULL) {
		for (i = 0; model->notifs[i] != NULL; i++) {
			free(model->notifs[i]);
		}
		free(model->notifs);
	}
	if (model->xml != NULL) {
		xmlFreeDoc(model->xml);
	}
	if (model->ctxt != NULL) {
		xmlXPathFreeContext(model->ctxt);
	}
	if (model->features != NULL) {
		for (i = 0; model->features[i] != NULL ; i++) {
			free(model->features[i]->name);
			free(model->features[i]);
		}
		free(model->features);
	}
	yinmodel_free(model->model_tree);

	free(model);
}

ncds_id ncds_init(struct ncds_ds* datastore)
{
	struct ncds_ds_list * item;

	/* not initiated datastores have id set to -1 */
	if (datastore == NULL || datastore->id != -1) {
		return -1;
	}

	/* check the size of datastore list */
	if ((ncds.count + 1) >= ncds.array_size) {
		void *tmp = realloc(ncds.datastores_ids, (ncds.array_size + 10) * sizeof(ncds_id));
		if (tmp == NULL) {
			ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
			return (-4);
		}
		ncds.datastores_ids = tmp;
		ncds.array_size += 10;
	}
	/* prepare slot for the datastore in the list */
	item = malloc(sizeof(struct ncds_ds_list));
	if (item == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return -4;
	}

	/** \todo data model validation */

	/* call implementation-specific datastore init() function */
	if (datastore->func.init(datastore) != 0) {
		free(item);
		return -2;
	}

	/* acquire unique id */
	datastore->id = generate_id();

	/* add to list */
	item->datastore = datastore;
	item->next = ncds.datastores;
	ncds.datastores = item;
	ncds.count++;

	return datastore->id;
}

void ncds_cleanall()
{
	struct ncds_ds_list *ds_item, *dsnext;
	struct model_list *listitem, *listnext;
	int i;

	ds_item = ncds.datastores;
	while (ds_item != NULL) {
		dsnext = ds_item->next;
		ncds_free(ds_item->datastore);
		ds_item = dsnext;
	}

	for (listitem = models_list; listitem != NULL; ) {
		listnext = listitem->next;
		ncds_ds_model_free(listitem->model);
		/* listitem is actually also freed by ncds_ds_model_free() */
		listitem = listnext;
	}

	for (i = 0; models_dirs != NULL && models_dirs[i] != NULL; i++) {
		free(models_dirs[i]);
	}
	free(models_dirs);
	models_dirs = NULL;
}

void ncds_free(struct ncds_ds* datastore)
{
	struct ncds_ds *ds = NULL;

	if (datastore == NULL) {
		WARN("%s: no datastore to free.", __func__);
		return;
	}

	if (datastore->id != -1) {
		/* datastore is initialized and must be in the datastores list */
		ds = datastores_detach_ds(datastore->id);
	} else {
		/* datastore to free is uninitialized and will be only freed */
		ds = datastore;
	}

	/* close and free the datastore itself */
	if (ds != NULL) {
		/* if using transapi */
		if (ds->transapi.module != NULL) {
			/* and close function is defined */
			if (ds->transapi.close != NULL) {
				ds->transapi.close();
			}
			/* unloading transapi module failed */
			if (dlclose(ds->transapi.module)) {
				ERROR("%s: Unloading transAPI module failed: %s:", __func__, dlerror());
			}
		}

#ifndef DISABLE_VALIDATION
		/* validators */
		xmlRelaxNGFreeValidCtxt(ds->validators.rng);
		xmlRelaxNGFree(ds->validators.rng_schema);
		xsltFreeStylesheet(ds->validators.schematron);
#endif
		/* free all implementation specific resources */
		datastore->func.free(ds);

		/* free models */
		if (ds->data_model->xml != ds->ext_model) {
			xmlFreeDoc(ds->ext_model);
		}
		ncds_ds_model_free(ds->data_model);

		free (ds);
	}
}

void ncds_free2(ncds_id datastore_id)
{
	struct ncds_ds *del;

	/* empty list */
	if (ncds.datastores == NULL) {
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
		 * list and also the whole list item (del variable here) is freed
		 * by ncds_free(), so do not do it here!
		 */
		ncds_free(del);
	}
}

xmlDocPtr ncxml_merge(const xmlDocPtr first, const xmlDocPtr second, const xmlDocPtr data_model)
{
	int ret;
	keyList keys;
	xmlDocPtr result;

	/* return NULL if both docs are NULL, or the other doc in case on of them is NULL */
	if (first == NULL) {
		return (xmlCopyDoc(second, 1));
	} else if (second == NULL) {
		return (xmlCopyDoc(first, 1));
	}

	result = xmlCopyDoc(first, 1);
	if (result == NULL) {
		return (NULL);
	}

	/* get all keys from data model */
	keys = get_keynode_list(data_model);

	/* merge the documents */
	ret = edit_merge(result, second->children, data_model, keys, NULL, NULL);

	if (keys != NULL) {
		keyListFree(keys);
	}

	if (ret != EXIT_SUCCESS) {
		xmlFreeDoc(result);
		return (NULL);
	} else {
		return (result);
	}
}

/**
 * \brief compare the node properties against the reference node properties
 *
 * \param reference     reference node, compared node must have all the
 *                      properties (and the same values) as reference node
 * \param node          compared node
 *
 * \return              0 if compared node contains all the properties (with
 *						the same values) as reference node, 1 otherwise
 */
int attrcmp(xmlNodePtr reference, xmlNodePtr node)
{
	xmlAttrPtr attr = reference->properties;
	xmlChar *value = NULL, *refvalue = NULL;

	while (attr != NULL) {
		if ((value = xmlGetProp(node, attr->name)) == NULL) {
			return 1;
		} else {
			refvalue = xmlGetProp(reference, attr->name);
			if (strcmp((char *) refvalue, (char *) value)) {
				free(refvalue);
				free(value);
				return 1;
			}
			free(refvalue);
			free(value);
		}
		attr = attr->next;
	}

	return 0;
}

/**
 * \brief NETCONF subtree filtering, stolen from old old netopeer
 *
 * \param config        pointer to xmlNode tree to filter
 * \param filter        pointer to NETCONF filter xml tree
 *
 * \return              1 if config satisfies the output filter, 0 otherwise
 */

static int ncxml_subtree_filter(xmlNodePtr config, xmlNodePtr filter)
{
	xmlNodePtr config_node = config;
	xmlNodePtr filter_node = filter;
	xmlNodePtr delete = NULL, delete2 = NULL;
	char *content1 = NULL, *content2 = NULL;

	int filter_in = 0, sibling_in = 0, end_node = 0, sibling_selection = 0;

	/* check if this filter level is last */
	filter_node = filter;
	while (filter_node) {
		if ((filter_node->children) && (filter_node->children->type == XML_TEXT_NODE)) {
			end_node = 1;
			break;
		}
		filter_node = filter_node->next;
	}

	if (end_node) {
		/* try to find required node */
		config_node = config;
		while (config_node) {
			if (!strcmp((char *) filter_node->name, (char *) config_node->name) &&
					!nc_nscmp(filter_node, config_node) &&
					!attrcmp(filter_node, config_node)) {
				filter_in = 1;
				break;
			}
			config_node = config_node->next;
		}

		/* if required node is present, decide about removing sibling nodes */
		if (filter_in) {
			/* 0 means that all the sibling nodes will be in the filter result - this is a default
			 * behavior when there are no selection or containment nodes in the filter sibling set.
			 * If 1 is set, sibling nodes for the filter result will be selected according to the
			 * rules in RFC 6242, sec. 6.2.5
			 */
			sibling_selection = 0;

			/* choose kind of used filter node */
			if (config_node->children && (config_node->children->type == XML_TEXT_NODE)) {
				/* get filter's text node content ignoring whitespaces */
				if ((content1 = nc_clrwspace((char *) filter_node->children->content)) == NULL ||
						(content2 = nc_clrwspace((char *) config_node->children->content)) == NULL) {
					free(content1);
					free(content2);
					/* internal error - memory allocation failed, do not continue! */
					return 0;
				}
				if (strisempty(content1)) {
					/* we have an empty content match node, so interpret it as a selection node,
					 * which means that we will be selecting sibling nodes that will be in the
					 * filter result
					 */
					sibling_selection = 1;
				} else if (strcmp(content1, content2) != 0) {
					free(content1);
					free(content2);
					/* content match node doesn't match */
					return 0;
				}
				free(content1);
				free(content2);
			}
			if (filter_node->next || filter_node->prev || sibling_selection == 1) {
				/* check if all filter sibling nodes are content match nodes -> then no config sibling node will be removed */
				/*go to the first filter sibling node */
				filter_node = filter;
				/* pass all filter sibling nodes */
				while (sibling_selection == 0 && filter_node) {
					if (!filter_node->children || (filter_node->children->type != XML_TEXT_NODE)) {
						sibling_selection = 1; /* filter result will be selected */
						break;
					}
					filter_node = filter_node->next;
				}

				/* select and remove all unwanted nodes */
				config_node = config;
				while (config_node) {
					sibling_in = 0;
					/* go to the first filter sibing node */
					filter_node = filter;
					/* pass all filter sibling nodes */
					while (filter_node) {
						if (!strcmp((char *) filter_node->name, (char *) config_node->name) &&
								!nc_nscmp(filter_node, config_node) &&
								!attrcmp(filter_node, config_node)) {
							/* content match node check */
							if (filter_node->children && (filter_node->children->type == XML_TEXT_NODE) &&
									config_node->children && (config_node->children->type == XML_TEXT_NODE)) {

								/* get filter's text node content ignoring whitespaces */
								if ((content1 = nc_clrwspace((char *) filter_node->children->content)) == NULL ||
										(content2 = nc_clrwspace((char *) config_node->children->content)) == NULL) {
									free(content1);
									free(content2);
									/* internal error - memory allocation failed, do not continue! */
									return 0;
								}
								if (strisempty(content1)) {
									/* we have an empty content match node, so interpret it as a selection node,
									 * which means that it will be included in the filter result
									 */
								} else if (strcmp(content1, content2) != 0) {
									free(content1);
									free(content2);
									/* content match node doesn't match */
									return 0;
								}
								free(content1);
								free(content2);
							}
							sibling_in = 1;
							break;
						}
						filter_node = filter_node->next;
					}
					/* if this config node is not in filter, remove it */
					if (sibling_selection && !sibling_in) {
						delete = config_node;
						config_node = config_node->next;
						xmlUnlinkNode(delete);
						xmlFreeNode(delete);
					} else {
						/* recursively process subtree filter */
						if (filter_node && filter_node->children && (filter_node->children->type == XML_ELEMENT_NODE) &&
								config_node->children && (config_node->children->type == XML_ELEMENT_NODE)) {
							sibling_in = ncxml_subtree_filter(config_node->children, filter_node->children);
						}
						if (sibling_selection && sibling_in == 0) {
							/* subtree is not a content of the filter output */
							delete = config_node;

							/* remeber where to go next */
							config_node = config_node->next;

							/* and remove unwanted subtree */
							xmlUnlinkNode(delete);
							xmlFreeNode(delete);
						} else {
							/* go to the next sibling */
							config_node = config_node->next;
						}
					}
				}
			} else {
				/* only content match node present - all sibling nodes stays */
			}
		}
	} else {
		/* this is containment node (no sibling node is content match node */
		filter_node = filter;
		while (filter_node) {
			if (!strcmp((char *)filter_node->name, (char *)config->name) &&
					!nc_nscmp(filter_node, config) &&
					!attrcmp(filter_node, config)) {
				filter_in = 1;
				break;
			}
			filter_node = filter_node->next;
		}

		if (filter_in == 1) {
			while (config->children && filter_node && filter_node->children &&
					((filter_in = ncxml_subtree_filter(config->children, filter_node->children)) == 0)) {
				filter_node = filter_node->next;
				while (filter_node) {
					if (!strcmp((char *)filter_node->name, (char *)config->name) &&
							!nc_nscmp(filter_node, config) &&
							!attrcmp(filter_node, config)) {
						filter_in = 1;
						break;
					}
					filter_node = filter_node->next;
				}
			}
			if (filter_in == 0) {
				/* subtree is not a content of the filter output */
				delete = config->children;
				xmlUnlinkNode(delete);
				xmlFreeNode(delete);
				delete2 = config;
			}
		} else {
			delete2 = config;
		}
		/* filter next sibling node */
		if (config->next != NULL) {
			if (ncxml_subtree_filter(config->next, filter) == 0) {
				delete = config->next;
				xmlUnlinkNode(delete);
				xmlFreeNode(delete);
			} else {
				filter_in = 1;
			}
		}
		if (delete2) {
			xmlUnlinkNode(delete2);
			xmlFreeNode(delete2);
		}
	}

	return filter_in;
}

int ncxml_filter(xmlNodePtr old, const struct nc_filter* filter, xmlNodePtr *new)
{
	xmlDocPtr result, data_filtered[2] = {NULL, NULL};
	xmlNodePtr filter_item, node;
	int ret = EXIT_FAILURE;

	if (new == NULL || old == NULL || filter == NULL) {
		return EXIT_FAILURE;
	}

	switch (filter->type) {
	case NC_FILTER_SUBTREE:
		if (filter->subtree_filter == NULL) {
			ERROR("%s: invalid filter (%s:%d).", __func__, __FILE__, __LINE__);
			return EXIT_FAILURE;
		}
		data_filtered[0] = xmlNewDoc(BAD_CAST "1.0");
		data_filtered[1] = xmlNewDoc(BAD_CAST "1.0");
		for (filter_item = filter->subtree_filter->children; filter_item != NULL; filter_item = filter_item->next) {
			xmlDocSetRootElement(data_filtered[0], xmlCopyNode(old, 1));
			ncxml_subtree_filter(data_filtered[0]->children, filter_item);
			if (data_filtered[1]->children == NULL) {
				if (data_filtered[0]->children != NULL) {
					/* we have some result */
					node = data_filtered[0]->children;
					xmlUnlinkNode(node);
					xmlDocSetRootElement(data_filtered[1], node);
				}
			} else {
				result = ncxml_merge(data_filtered[0], data_filtered[1], NULL);
				node = data_filtered[0]->children;
				xmlUnlinkNode(node);
				xmlFreeNode(node);
				xmlFreeDoc(data_filtered[1]);
				data_filtered[1] = result;
			}
		}
		if (filter->subtree_filter->children != NULL) {
			if(data_filtered[1]->children != NULL) {
				*new = xmlCopyNode(data_filtered[1]->children, 1);
			} else {
				*new = NULL;
			}
		} else { /* empty filter -> RFC 6241, sec. 6.4.2 - result is empty */
			*new = NULL;
		}
		xmlFreeDoc(data_filtered[0]);
		xmlFreeDoc(data_filtered[1]);
		ret = EXIT_SUCCESS;
		break;
	default:
		ret = EXIT_FAILURE;
		break;
	}

	return ret;
}

int ncds_rollback(ncds_id id)
{
	struct ncds_ds *datastore = datastores_get_ds(id);

	if (datastore == NULL) {
		return (EXIT_FAILURE);
	}

	return (datastore->func.rollback(datastore));
}

/**
 * @brief Check if source and target are same. If url is enabled, checks if source and target urls are same
 * @param rpc
 * @param session
 * @return 
 */
int ncds_is_conflict(const nc_rpc * rpc, const struct nc_session * session)
{
	NC_DATASTORE source, target;
#ifndef DISABLE_URL
	xmlXPathObjectPtr query_source = NULL;
	xmlXPathObjectPtr query_target = NULL;
	xmlChar *nc1 = NULL, *nc2 = NULL;
	int ret;
#endif /* DISABLE_URL */

	source = nc_rpc_get_source(rpc);
	target = nc_rpc_get_target(rpc);

	if (source == target) {
		/* source and target datastore are the same */
#ifndef DISABLE_URL
		/* if they are URLs, check if both URLs point to a single resource */
		if (source == NC_DATASTORE_URL && nc_cpblts_enabled(session, NC_CAP_URL_ID)) {
			query_source = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":url", rpc->ctxt);
			query_target = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":url", rpc->ctxt);
			if ((query_source == NULL || query_target == NULL )) {
				return 1;
			}

			nc1 = xmlNodeGetContent(query_source->nodesetval->nodeTab[0]);
			nc2 = xmlNodeGetContent(query_target->nodesetval->nodeTab[0]);

			if ((nc1 == NULL) || (nc2 == NULL)) {
				ERROR("Empty source or target in ncds_is_conflict");
				return 1;
			}
			ret = xmlStrcmp(nc1, nc2);

			/* cleanup */
			xmlFree(nc1);
			xmlFree(nc2);
			xmlXPathFreeObject(query_source);
			xmlXPathFreeObject(query_target);
			return (ret);
		} else {
#else
		{
#endif /* DISABLE_URL */
			/* rpc targets local datastores */
			return 1;
		}
	}

	return 0;
}

/**
 * \return NULL on success, error reply with error info else
 */
static nc_reply* ncds_apply_transapi(struct ncds_ds* ds, const struct nc_session* session, xmlDocPtr old, NC_EDIT_ERROPT_TYPE erropt, nc_reply *reply)
{
	char *new_data;
	xmlDocPtr new;
	xmlChar *config;
	int ret;
	struct nc_err *e = NULL, *e_new;
	nc_reply *new_reply = NULL;

	if (reply != NULL && nc_reply_get_type(reply) == NC_REPLY_ERROR) {
		/* use some reply to add new error messages */
		new_reply = reply;
	}

	/* find differences and call functions */
	new_data = ds->func.getconfig(ds, session, NC_DATASTORE_RUNNING, &e);
	if (new_data == NULL || strcmp(new_data, "") == 0) {
		new = xmlNewDoc(BAD_CAST "1.0");
	} else {
		new = xmlReadDoc(BAD_CAST new_data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	}
	free(new_data);
	if (new == NULL ) { /* cannot get or parse data */
		e = nc_err_new(NC_ERR_OP_FAILED);
		if (new_reply != NULL) {
			/* second try, add the error info */
			nc_err_set(e, NC_ERR_PARAM_MSG, "TransAPI: Failed to get data from RUNNING datastore. Configuration is probably inconsistent.");
			nc_reply_error_add(new_reply, e);
		} else {
			nc_err_set(e, NC_ERR_PARAM_MSG, "TransAPI: Failed to get data from RUNNING datastore.");
			new_reply = nc_reply_error(e);
		}
	} else {
		/* announce error-option to the TransAPI module, if error-option not set, announce default stop-on-error */
		*(ds->transapi.erropt) = (erropt != NC_EDIT_ERROPT_NOTSET) ? erropt : NC_EDIT_ERROPT_STOP;
		/* perform TransAPI transactions */
		ret = transapi_running_changed(ds->transapi.data_clbks, ds->transapi.ns_mapping, old, new, ds->data_model, erropt, &e);
		if (ret) {
			e_new = nc_err_new(NC_ERR_OP_FAILED);
			if (e != NULL) {
				/* remember the error description from TransAPI */
				e_new->next = e;
				e = NULL;
			}
			if (new_reply != NULL ) {
				/* second try, add the error info */
				nc_err_set(e_new, NC_ERR_PARAM_MSG, "Failed to rollback configuration changes to device. Configuration is probably inconsistent.");
				nc_reply_error_add(new_reply, e_new);
			} else {
				nc_err_set(e_new, NC_ERR_PARAM_MSG, "Failed to apply configuration changes to device.");
				new_reply = nc_reply_error(e_new);

				if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
					/* do the rollback on datastore */
					ds->func.rollback(ds);
				}

			}
		} /* else success */

		if (ret || *ds->transapi.config_modified) {
			ds->transapi.config_modified = 0;
			DBG("Updating XML tree after TransAPI callbacks");
			xmlDocDumpMemory(new, &config, NULL);
			if (ds->func.copyconfig(ds, session, NULL, NC_DATASTORE_RUNNING, NC_DATASTORE_CONFIG, (char*)config, &e) == EXIT_FAILURE) {
				ERROR("transAPI apply failed");
			}
			xmlFree(config);
		}
		xmlFreeDoc(new);
	}

	return (new_reply);
}

nc_reply* ncds_apply_rpc(ncds_id id, const struct nc_session* session, const nc_rpc* rpc)
{
	struct nc_err* e = NULL;
	struct ncds_ds* ds = NULL;
	struct nc_filter * filter = NULL;
	char* data = NULL, *config, *model = NULL, *data2, *op_name;
	xmlDocPtr doc1, doc2, doc_merged = NULL, aux_doc;
	int len, dsid, i, j;
	int ret = EXIT_FAILURE;
	nc_reply* reply = NULL, *old_reply = NULL, *new_reply;
	xmlBufferPtr resultbuffer;
	xmlNodePtr aux_node, node;
	NC_OP op;
	xmlDocPtr old = NULL;
	char * old_data = NULL;
	NC_DATASTORE source_ds = 0, target_ds = 0;
	struct nacm_rpc *nacm_aux;
	nc_rpc *rpc_aux;
	void * op_input_array;
	xmlNodePtr op_node;
	xmlNodePtr op_input;
	int pos;
	int transapi_callbacks_count;
	const char * rpc_name;
	char *end = NULL, *aux = NULL;
#ifndef DISABLE_VALIDATION
	NC_EDIT_TESTOPT_TYPE testopt;
#endif

#ifndef DISABLE_URL
	xmlXPathObjectPtr url_path = NULL;
	xmlChar *ncontent;
	xmlNodePtr url_remote_node;
	xmlNodePtr url_local_node;
	xmlNodePtr url_tmp_node;
	xmlNodePtr url_tmp_node_next;
	xmlXPathObjectPtr url_model_xpath = NULL;
	xmlChar * url_model_namespace;
	xmlChar * url_model_name;
	xmlDocPtr url_tmp_doc;
	xmlDocPtr url_local_doc;
	xmlChar * url_doc_text;
	char url_test_empty;
	int url_tmpfile;
	xmlNsPtr url_new_ns;
	NC_URL_PROTOCOLS protocol;
#endif /* DISABLE_URL */

	if (rpc == NULL || session == NULL) {
		ERROR("%s: invalid parameter %s", __func__, (rpc==NULL)?"rpc":"session");
		return (NULL);
	}

	dsid = id;

process_datastore:

	ds = datastores_get_ds(dsid);
	if (ds == NULL) {
		return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
	}

	op = nc_rpc_get_op(rpc);
	/* if transapi used AND operation will affect running repository => store current running content */

	if (ds->transapi.module != NULL
		&& (op == NC_OP_COMMIT || op == NC_OP_COPYCONFIG || (op == NC_OP_EDITCONFIG && (nc_rpc_get_testopt(rpc) != NC_EDIT_TESTOPT_TEST))) &&
		(nc_rpc_get_target(rpc) == NC_DATASTORE_RUNNING)) {

		old_data = ds->func.getconfig(ds, session, NC_DATASTORE_RUNNING, &e);
		if (old_data == NULL || strcmp(old_data, "") == 0) {
			old = xmlNewDoc (BAD_CAST "1.0");
		} else {
			old = xmlReadDoc(BAD_CAST old_data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		}
		free(old_data);
		if (old == NULL) {/* cannot get or parse data */
			if (e == NULL) { /* error not set */
				e = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(e, NC_ERR_PARAM_MSG, "TransAPI: Failed to get data from RUNNING datastore.");
			}
			return nc_reply_error(e);
		}
	}

	switch (op) {
	case NC_OP_LOCK:
		ret = ds->func.lock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_UNLOCK:
		ret = ds->func.unlock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_GET:
		if ((data = ds->func.getconfig(ds, session, NC_DATASTORE_RUNNING, &e)) == NULL ) {
			if (e == NULL ) {
				ERROR("%s: Failed to get data from the datastore (%s:%d).", __func__, __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
			}
			break;
		}

		if (ds->get_state_xml != NULL || ds->get_state != NULL) {
			/* caller provided callback function to retrieve status data */

			if (ds->get_state_xml != NULL) {
				/* status data are directly in XML format */
				doc2 = ds->get_state_xml(ds->ext_model, doc1, &e);
			} else if (ds->get_state != NULL) {
				/* status data are provided as string, convert it into XML structure */
				xmlDocDumpMemory(ds->ext_model, (xmlChar**) (&model), &len);
				data2 = ds->get_state(model, data, &e);
				doc2 = xmlReadDoc(BAD_CAST data2, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
				free(model);
				free(data2);
			} else {
				/* we have no status data */
				doc2 = NULL;
			}

			if (e != NULL) {
				/* state data retrieval error */
				free(data);
				break;
			}

			/* convert configuration data into XML structure */
			doc1 = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

			/* merge status and config data */
			/* if merge fail (probably one of docs NULL)*/
			if ((doc_merged = ncxml_merge(doc1, doc2, ds->ext_model)) == NULL) {
				/* use only config if not null*/
				if (doc1 != NULL) {
					doc_merged = doc1;
					xmlFreeDoc(doc2);
					/* or only state if not null*/
				} else if (doc2 != NULL) {
					doc_merged = doc2;
					xmlFreeDoc(doc1);
					/* or create empty document to allow further processing */
				} else {
					doc_merged = xmlNewDoc(BAD_CAST "1.0");
					xmlFreeDoc(doc1);
					xmlFreeDoc(doc2);
				}
			} else {
				/* cleanup */
				xmlFreeDoc(doc1);
				xmlFreeDoc(doc2);
			}
		} else {
			data2 = data;
			if (strncmp(data2, "<?xml", 5) == 0) {
				/* We got a "real" XML document. We strip off the
				 * declaration, so the thing below works.
				 *
				 * We just replace that with whitespaces, which is
				 * harmless, but we'll free the correct pointer.
				 */
				end = index(data2, '>');
				if (end != NULL) {
					for (aux = data2; aux <= end; aux++) {
						*aux = ' ';
					}
				} /* else content is corrupted that will be detected by xmlReadDoc() */
			}
			if (asprintf(&data, "<data>%s</data>", data2) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
				break;
			}
			aux_doc = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			if (aux_doc && aux_doc->children) {
				doc_merged = xmlNewDoc(BAD_CAST "1.0");
				for (aux_node = aux_doc->children->children; aux_node != NULL; aux_node = aux_node->next) {
					if (doc_merged->children == NULL) {
						xmlDocSetRootElement(doc_merged, xmlCopyNode(aux_node, 1));
					} else {
						xmlAddSibling(doc_merged->children, xmlCopyNode(aux_node, 1));
					}
				}
				xmlFreeDoc(aux_doc);
			}
			free(data2);
		}
		free(data);

		if (doc_merged == NULL) {
			ERROR("Reading the configuration datastore failed.");
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid datastore content.");
			break;
		}

		/* process default values */
		if (ds && ds->data_model->xml) {
			ncdflt_default_values(doc_merged, ds->ext_model, rpc->with_defaults);
		}

		/* NACM */
		nacm_check_data_read(doc_merged, rpc->nacm);

		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			e = nc_err_new(NC_ERR_OP_FAILED);
			break;
		}

		/* if filter specified, now is good time to apply it */
		filter = nc_rpc_get_filter(rpc);
		for (aux_node = doc_merged->children; aux_node != NULL; aux_node = aux_node->next) {
			if (filter != NULL) {
				if (ncxml_filter(aux_node, filter, &node) != 0) {
					ERROR("Filter failed.");
					e = nc_err_new(NC_ERR_BAD_ELEM);
					nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
					nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "filter");
					break;
				}
			} else {
				node = xmlCopyNode(aux_node, 1);
			}
			if (node != NULL) {
				xmlNodeDump(resultbuffer, NULL, node, 2, 1);
				xmlFreeNode(node);
				node = NULL;
			}
		}
		nc_filter_free(filter);
		data = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(doc_merged);

		break;
	case NC_OP_GETCONFIG:
		if ((data = ds->func.getconfig(ds, session, nc_rpc_get_source(rpc), &e)) == NULL) {
			if (e == NULL) {
				ERROR ("%s: Failed to get data from the datastore (%s:%d).", __func__, __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
			}
			break;
		}
		if (strcmp(data, "") == 0) {
			doc_merged = xmlNewDoc(BAD_CAST "1.0");
		} else {
			data2 = data;
			if (asprintf(&data, "<data>%s</data>", data2) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
				break;
			}
			aux_doc = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			if (aux_doc && aux_doc->children) {
				doc_merged = xmlNewDoc(BAD_CAST "1.0");
				for (aux_node = aux_doc->children->children; aux_node != NULL; aux_node = aux_node->next) {
					if (doc_merged->children == NULL) {
						xmlDocSetRootElement(doc_merged, xmlCopyNode(aux_node, 1));
					} else {
						xmlAddSibling(doc_merged->children, xmlCopyNode(aux_node, 1));
					}
				}
				xmlFreeDoc(aux_doc);
			}
			free(data2);
		}
		free(data);

		if (doc_merged == NULL) {
			ERROR("Reading configuration datastore failed.");
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid datastore content.");
			break;
		}

		/* process default values */
		if (ds && ds->data_model->xml) {
			ncdflt_default_values(doc_merged, ds->ext_model, rpc->with_defaults);
		}

		/* NACM */
		nacm_check_data_read(doc_merged, rpc->nacm);

		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			e = nc_err_new(NC_ERR_OP_FAILED);
			break;
		}

		/* if filter specified, now is good time to apply it */
		for (aux_node = doc_merged->children; aux_node != NULL; aux_node = aux_node->next) {
			if ((filter = nc_rpc_get_filter(rpc)) != NULL) {
				if (ncxml_filter(aux_node, filter, &node) != 0) {
					ERROR("Filter failed.");
					e = nc_err_new(NC_ERR_BAD_ELEM);
					nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
					nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "filter");
					break;
				}	
			} else {
				node = xmlCopyNode(aux_node, 1);
			}
			if (node != NULL) {
				xmlNodeDump(resultbuffer, NULL, node, 2, 1);
				xmlFreeNode(node);
				node = NULL;
			}
		}
		nc_filter_free(filter);
		data = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(doc_merged);

		break;
	case NC_OP_EDITCONFIG:
	case NC_OP_COPYCONFIG:
		if (ds->type == NCDS_TYPE_EMPTY) {
			/* there is nothing to edit in empty datastore type */
			ret = EXIT_RPC_NOT_APPLICABLE;
			data = NULL;
			break;
		}

		/* check target element */
		if ((target_ds = nc_rpc_get_target(rpc)) == NC_DATASTORE_ERROR) {
			e = nc_err_new(NC_ERR_BAD_ELEM);
			nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "target");
			break;
		}
		/* check source element */
		if (op == NC_OP_COPYCONFIG && (source_ds = nc_rpc_get_source(rpc)) == NC_DATASTORE_ERROR) {
			e = nc_err_new(NC_ERR_BAD_ELEM);
			nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "source");
			break;
		}

		if (op == NC_OP_COPYCONFIG && ((source_ds != NC_DATASTORE_CONFIG) && (source_ds != NC_DATASTORE_URL ))) {
			/* <copy-config> with a standard datastore as a source */
			/* check possible conflicts */
			if (ncds_is_conflict(rpc, session) ) {
				e = nc_err_new(NC_ERR_INVALID_VALUE);
				nc_err_set(e, NC_ERR_PARAM_MSG, "Both the target and the source identify the same datastore.");
				break;
			}
			config = NULL;
		} else {
			/* source is url or config, here starts woodo magic */
			/*
			 * if configuration data are provided as operation's config,
			 * just return <config> element content. If it is url,
			 * download remote file and return its content
			 */
			config = nc_rpc_get_config(rpc);
			if (config == NULL) {
				e = nc_err_new(NC_ERR_OP_FAILED);
				break;
			}
			if (strcmp(config, "") == 0) {
				/* config is empty -> ignore rest of magic here,
				 * go to application of the operation and do
				 * delete of the datastore (including running)!
				 */
				goto apply_editcopyconfig;
			}

			/*
			 * config can contain multiple elements on the root level, so
			 * cover it with the <config> element to allow the creation of
			 * xml document
			 */
			if (asprintf(&data, "<config>%s</config>", config) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
				break;
			}
			free(config);
			config = NULL;
			doc1 = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			free(data);
			data = NULL;

			if (doc1 == NULL || doc1->children == NULL || doc1->children->children == NULL) {
				if (doc1 != NULL) {
					xmlFreeDoc(doc1);
				}
				e = nc_err_new(NC_ERR_INVALID_VALUE);
				nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid <config> parameter of the rpc request.");
				break;
			}

			/*
			 * select correct config node for the selected datastore,
			 * it must match the model's namespace and root element name
			 */
			aux_node = get_model_root(doc1->children->children, ds->data_model);
			if (aux_node != NULL) {
				resultbuffer = xmlBufferCreate();
				if (resultbuffer == NULL) {
					ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
					e = nc_err_new(NC_ERR_OP_FAILED);
					nc_err_set(e, NC_ERR_PARAM_MSG, "Internal error, see libnetconf error log.");
					break;
				}
				xmlNodeDump(resultbuffer, doc1, aux_node, 2, 1);
				if ((config = strdup((char*) xmlBufferContent(resultbuffer))) == NULL) {
					xmlBufferFree(resultbuffer);
					xmlFreeDoc(doc1);
					ERROR("%s: xmlBufferContent failed (%s:%d)", __func__, __FILE__, __LINE__);
					e = nc_err_new(NC_ERR_OP_FAILED);
					nc_err_set(e, NC_ERR_PARAM_MSG, "Internal error, see libnetconf error log.");
					break;
				}
				/*
				 * now we have config as a valid xml tree with the
				 * single root
				 */
				xmlBufferFree(resultbuffer);
				xmlFreeDoc(doc1);
			} else {
				xmlFreeDoc(doc1);
				/* request is not intended for this device */
				ret = EXIT_RPC_NOT_APPLICABLE;
				data = NULL;
				break;
			}

			/* do some work in case of used with-defaults capability */
			if (rpc->with_defaults & NCWD_MODE_ALL_TAGGED) {
				/* if report-all-tagged mode is supported, 'default'
				 * attribute with 'true' or '1' value can appear and we
				 * have to check that the element's value is equal to the
				 * default value. If it is, the element is removed and
				 * is supposed to be default, otherwise the
				 * invalid-value error reply must be returned.
				 */
				doc1 = xmlReadDoc(BAD_CAST config, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
				free(config);

				if (ncdflt_default_clear(doc1, ds->ext_model) != EXIT_SUCCESS) {
					e = nc_err_new(NC_ERR_INVALID_VALUE);
					nc_err_set(e, NC_ERR_PARAM_MSG, "with-defaults capability failure");
					break;
				}
				xmlDocDumpFormatMemory(doc1, (xmlChar**) (&config), &len, 1);
				xmlFreeDoc(doc1);
			}
		}
apply_editcopyconfig:
		/* perform the operation */
		if (op == NC_OP_EDITCONFIG) {
			ret = ds->func.editconfig(ds, session, rpc, target_ds, config, nc_rpc_get_defop(rpc), nc_rpc_get_erropt(rpc), &e);
#ifndef DISABLE_VALIDATION
			if (ret == EXIT_SUCCESS) {
				/* process test option if set */
				switch (testopt = nc_rpc_get_testopt(rpc)) {
				case NC_EDIT_TESTOPT_TEST:
				case NC_EDIT_TESTOPT_TESTSET:
					/* validate the result */
					ret = apply_rpc_validate_(ds, session, target_ds, NULL, &e);

					if (ret == EXIT_RPC_NOT_APPLICABLE) {
						/*
						 * we don't have enough information to do
						 * validation (validators are missing), so
						 * just say that it is ok and allow to
						 * perform the required changes. In validate
						 * operation we return RPC not applicable if
						 * no datastore provides validation, but
						 * in edit-config, we don't know if there
						 * is any datastore with validation since
						 * it doesn't need to affect all datastores.
						 */
						ret = EXIT_SUCCESS;
					}

					if (testopt == NC_EDIT_TESTOPT_TEST || ret == EXIT_FAILURE) {
						/*
						 * revert changes in the datastore:
						 * only test was required or error occurred
						 */
						ds->func.rollback(ds);
					}

					break;
				default:
					/* continue without validation */
					break;
				}
			}
#endif
		} else if (op == NC_OP_COPYCONFIG) {
#ifndef DISABLE_URL
			if (source_ds == NC_DATASTORE_URL) {
				/* if source is url, change source type to config */
				source_ds = NC_DATASTORE_CONFIG;
				if (target_ds == NC_DATASTORE_URL) {
					/* if target is url, prepare document content */
					data = config;
					config = NULL;
					if (asprintf(&config, "<?xml version=\"1.0\"?><config xmlns=\""NC_NS_BASE10"\">%s</config>", data) == -1) {
						ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
						e = nc_err_new(NC_ERR_OP_FAILED);
						nc_err_set(e, NC_ERR_PARAM_MSG, "libnetconf server internal error, see error log.");
						free(data);
						data = NULL;
						break; /* main switch */
					}
					free(data);
					data = NULL;
				}
			}
			if (target_ds == NC_DATASTORE_URL && nc_cpblts_enabled(session, NC_CAP_URL_ID)) {
				/* get target url */
				url_path = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":url", rpc->ctxt);
				if (url_path == NULL || xmlXPathNodeSetIsEmpty(url_path->nodesetval)) {
					ERROR("%s: unable to get URL path from <copy-config> request.", __func__);
					e = nc_err_new(NC_ERR_BAD_ELEM);
					nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "target");
					nc_err_set(e, NC_ERR_PARAM_MSG, "Unable to get URL path from the <copy-config> request.");
					xmlXPathFreeObject(url_path);
					break; /* main switch */
				}
				ncontent = xmlNodeGetContent(url_path->nodesetval->nodeTab[0]);
				xmlXPathFreeObject(url_path);

				protocol = nc_url_get_protocol((char*)ncontent);
				if (protocol == 0 || !nc_url_is_enabled(protocol)) {
					ERROR("%s: protocol (%s - %d) not supported", __func__, ncontent, protocol);
					e = nc_err_new(NC_ERR_OP_FAILED);
					nc_err_set(e, NC_ERR_PARAM_MSG, "Specified URL protocol not supported.");
					xmlFree(ncontent);
					break; /* main switch */
				}

				switch (source_ds) {
				case NC_DATASTORE_CONFIG:
					/* source datastore is config (or url), so just upload file */
					ret = nc_url_upload(config, (char*)ncontent);
					break;
				case NC_DATASTORE_RUNNING:
				case NC_DATASTORE_STARTUP:
				case NC_DATASTORE_CANDIDATE:
					/* Woodoo magic.
					 * If target is URL we have problem, because ncds_apply_rpc2all is calling ncds_apply_rpc for
					 * each datastore -> remote file would be overwriten everytime. So solution is to download
					 * remote file, make document from it and add current datastore configuration data to documtent and
					 * then upload it. Problem is if remote file is not empty (it contains data from datastores we does not have).
					 * Then data would merge and we will have merged wanted data with non-wanted data from remote file before editing.
					 * Thats FEATURE, not bug!!!. I reccomend to call ncds_apply_rpc2all and before that use delete-config on remote file.
					 */
					/* get data from remote file */
					if ((url_tmpfile = nc_url_open((char*) ncontent)) < 0) {
						/*
						 * remote file is empty or does not exists,
						 * so create empty document with <config> root element
						 */
						url_tmp_doc = xmlNewDoc(BAD_CAST "1.0");
						url_remote_node = xmlNewNode(NULL, BAD_CAST "config");
						url_new_ns = xmlNewNs(url_remote_node, BAD_CAST NC_NS_BASE10, NULL);
						xmlSetNs(url_remote_node, url_new_ns);
						xmlDocSetRootElement(url_tmp_doc, url_remote_node);
					} else {
						if (read(url_tmpfile, &url_test_empty, 1) > 0) {
							/* file is not empty */
							lseek(url_tmpfile, 0, SEEK_SET);
							if ((url_tmp_doc = xmlReadFd(url_tmpfile, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING)) == NULL) {
								close(url_tmpfile);
								ERROR("%s: error reading XML data from tmp file", __func__);
								e = nc_err_new(NC_ERR_OP_FAILED);
								nc_err_set(e, NC_ERR_PARAM_MSG, "libnetconf internal server error, see error log.");
								break;
							}
							close(url_tmpfile);
							url_remote_node = xmlDocGetRootElement(url_tmp_doc);
							if (xmlStrcmp(BAD_CAST "config", url_remote_node->name) != 0) {
								ERROR("%s: no config data in remote file (%s)", __func__, ncontent);
								e = nc_err_new(NC_ERR_OP_FAILED);
								nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid remote configuration file, missing <config> element.");
								break;
							}

							/**
							 * first we remove all entries from "remote" document which match enabled data models
							 * this will prevent us from having multiple datastore configurations
							 */
							/* get data models */
							url_model_xpath = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":namespace", ds->data_model->ctxt);
							url_model_namespace = xmlGetProp(url_model_xpath->nodesetval->nodeTab[0], BAD_CAST "uri");
							xmlXPathFreeObject(url_model_xpath);

							url_model_xpath = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_YIN_ID":module/"NC_NS_YIN_ID":container", ds->data_model->ctxt);
							url_model_name = xmlGetProp(url_model_xpath->nodesetval->nodeTab[0], BAD_CAST "name");
							xmlXPathFreeObject(url_model_xpath);

							/* remove all remote entries which match data models from datastore */
							url_tmp_node = url_remote_node->children;
							while (url_tmp_node != NULL) {
								url_tmp_node_next = url_tmp_node->next;
								if (xmlStrcmp(url_tmp_node->name, url_model_name) == 0 &&
										(xmlStrcmp(url_tmp_node->ns->href, url_model_namespace) == 0)) {
									xmlUnlinkNode(url_tmp_node);
									xmlFreeNode(url_tmp_node);
								}

								url_tmp_node = url_tmp_node_next;
							}
							xmlFree(url_model_name);
							xmlFree(url_model_namespace);

						} else {
							/* file is empty, create new document with root <config> element */
							url_tmp_doc = xmlNewDoc(BAD_CAST "1.0");
							url_remote_node = xmlNewNode(NULL, BAD_CAST "config");
							xmlDocSetRootElement(url_tmp_doc, url_remote_node);
						}
					}

					data = ds->func.getconfig(ds, session, source_ds, &e);
					if (data == NULL ) {
						if (e == NULL ) {
							ERROR("%s: Failed to get data from the datastore (%s:%d).", __func__, __FILE__, __LINE__);
							e = nc_err_new(NC_ERR_OP_FAILED);
						}
						xmlFreeDoc(url_tmp_doc);
						break;
					}
					/* config == NULL */
					if (asprintf(&config, "<?xml version=\"1.0\"?><config xmlns=\""NC_NS_BASE10"\">%s</config>", data) == -1) {
						ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
						e = nc_err_new(NC_ERR_OP_FAILED);
						nc_err_set(e, NC_ERR_PARAM_MSG, "libnetconf server internal error, see error log.");

						/* cleanup */
						xmlFreeDoc(url_tmp_doc);
						free(data);
						data = NULL;
						break;
					}
					free(data);
					data = NULL;

					/* copy local data to "remote" document */
					url_local_doc = xmlParseMemory(config, strlen(config));
					url_local_node = xmlDocGetRootElement(url_local_doc);
					url_tmp_node = url_local_node->children;
					while (url_tmp_node != NULL) {
						url_tmp_node_next = url_tmp_node->next;
						xmlAddChild(url_remote_node, url_tmp_node);
						url_tmp_node = url_tmp_node_next;
					}

					xmlDocDumpMemory(url_tmp_doc, &url_doc_text, NULL);
					nc_url_upload((char*) url_doc_text, (char*) ncontent);
					xmlFree(url_doc_text);
					xmlFreeDoc(url_tmp_doc);

					break;
				default:
					ERROR("%s: invalid source datastore for URL target", __func__);
					e = nc_err_new(NC_ERR_BAD_ELEM);
					nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "source");
					nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid source element value for use with URL target.");
					break;
				}
				xmlFree(ncontent);

				if (e == NULL) {
					ret = EXIT_SUCCESS;
				} else {
					free(config);
					break; /* main switch */
				}
			} else {
#else
			{
#endif /* DISABLE_URL */
				ret = ds->func.copyconfig(ds, session, rpc, target_ds, source_ds, config, &e);
			}
		} else {
			ret = EXIT_FAILURE;
		}
		free(config);

#ifndef DISABLE_NOTIFICATIONS
		/* log the event */
		if (ret == EXIT_SUCCESS && (target_ds == NC_DATASTORE_RUNNING || target_ds == NC_DATASTORE_STARTUP)) {
			ncntf_event_new(-1, NCNTF_BASE_CFG_CHANGE, target_ds, NCNTF_EVENT_BY_USER, session);
		}
#endif /* DISABLE_NOTIFICATIONS */

		break;
	case NC_OP_DELETECONFIG:
		if (ds->type == NCDS_TYPE_EMPTY) {
			/* there is nothing to edit in empty datastore type */
			ret = EXIT_RPC_NOT_APPLICABLE;
			data = NULL;
			break;
		}

		if (nc_rpc_get_target(rpc) == NC_DATASTORE_RUNNING) {
			/* can not delete running */
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Cannot delete a running datastore.");
			break;
		}
		target_ds  = nc_rpc_get_target(rpc);
#ifndef DISABLE_URL
		if (target_ds == NC_DATASTORE_URL && nc_cpblts_enabled(session, NC_CAP_URL_ID)) {
			url_path = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/"NC_NS_BASE10_ID":delete-config/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":url", rpc->ctxt);
			if (url_path == NULL || xmlXPathNodeSetIsEmpty(url_path->nodesetval)) {
				ERROR("%s: unable to get URL path from <delete-config> request.", __func__);
				e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "target");
				nc_err_set(e, NC_ERR_PARAM_MSG, "Unable to get URL path from the <delete-config> request.");
				xmlXPathFreeObject(url_path);
				ret = EXIT_FAILURE;
				break;
			}
			ncontent = xmlNodeGetContent(url_path->nodesetval->nodeTab[0]);
			xmlXPathFreeObject(url_path);

			protocol = nc_url_get_protocol((char*) ncontent);
			if (protocol == 0 || !nc_url_is_enabled(protocol)) {
				ERROR("%s: protocol (%s - %d) not supported", __func__, ncontent, protocol);
				e = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(e, NC_ERR_PARAM_MSG, "Specified URL protocol not supported.");
				xmlFree(ncontent);
				break; /* main switch */
			}

			ret = nc_url_delete_config((char*) ncontent);
			xmlFree(ncontent);
		} else {
#else
		{
#endif /* DISABLE_URL */
			ret = ds->func.deleteconfig(ds, session, target_ds, &e);
		}
#ifndef DISABLE_NOTIFICATIONS
		/* log the event */
		if (ret == EXIT_SUCCESS && (target_ds == NC_DATASTORE_RUNNING || target_ds == NC_DATASTORE_STARTUP)) {
			ncntf_event_new(-1, NCNTF_BASE_CFG_CHANGE, target_ds, NCNTF_EVENT_BY_USER, session);
		}
#endif /* DISABLE_NOTIFICATIONS */

		break;
	case NC_OP_COMMIT:
		/* \todo check somehow, that candidate is not locked by another session */

		if (ds->type == NCDS_TYPE_EMPTY) {
			/* there is nothing to edit in empty datastore type */
			ret = EXIT_RPC_NOT_APPLICABLE;
			data = NULL;
			break;
		}

		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			ret = ds->func.copyconfig (ds, session, rpc, NC_DATASTORE_RUNNING, NC_DATASTORE_CANDIDATE, NULL, &e);

#ifndef DISABLE_NOTIFICATIONS
			/* log the event */
			if (ret == EXIT_SUCCESS) {
				ncntf_event_new (-1, NCNTF_BASE_CFG_CHANGE, NC_DATASTORE_RUNNING, NCNTF_EVENT_BY_USER, session);
			}
#endif /* DISABLE_NOTIFICATIONS */

		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
		break;
	case NC_OP_DISCARDCHANGES:
		if (ds->type == NCDS_TYPE_EMPTY) {
			/* there is nothing to edit in empty datastore type */
			ret = EXIT_RPC_NOT_APPLICABLE;
			data = NULL;
			break;
		}

		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			/* NACM - no datastore permissions are needed,
			 * so create a copy of the rpc and remove NACM structure
			 */
			rpc_aux = nc_msg_dup((struct nc_msg*)rpc);
			nacm_aux = rpc_aux->nacm;
			rpc_aux->nacm = NULL;
			ret = ds->func.copyconfig(ds, session, rpc_aux, NC_DATASTORE_CANDIDATE, NC_DATASTORE_RUNNING, NULL, &e);
			rpc_aux->nacm = nacm_aux;
			nc_rpc_free(rpc_aux);
		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
		break;
	case NC_OP_GETSCHEMA:
		if (nc_cpblts_enabled (session, NC_CAP_MONITORING_ID)) {
			if (dsid == NCDS_INTERNAL_ID) {
				if ((data = get_schema (rpc, &e)) == NULL) {
					ret = EXIT_FAILURE;
				} else {
					ret = EXIT_SUCCESS;
				}
			} else {
				data = strdup ("");
				ret = EXIT_SUCCESS;
			}
		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
		break;
#ifndef DISABLE_VALIDATION
	case NC_OP_VALIDATE:
		ret = apply_rpc_validate(ds, session, rpc, &e);
		break;
#endif
	case NC_OP_UNKNOWN:
		/* get operation name */
		op_name = nc_rpc_get_op_name (rpc);
		/* prepare for case RPC is not supported by this datastore */
		reply = NCDS_RPC_NOT_APPLICABLE;
		/* go through all RPC implemented by datastore */
		if (ds->transapi.module) {
			transapi_callbacks_count = ds->transapi.rpc_clbks->callbacks_count;
			for (i=0; i<transapi_callbacks_count; i++) {
				/* find matching rpc and call rpc callback function */
				rpc_name = ds->transapi.rpc_clbks->callbacks[i].name;
				if (strcmp(op_name, rpc_name) == 0) {
					/* create array of input parameters */
					op_input_array = calloc(ds->transapi.rpc_clbks->callbacks[i].arg_count, sizeof (xmlNodePtr));
					/* get operation node */
					op_node = ncxml_rpc_get_op_content(rpc);
					op_input = op_node->children;
					while (op_input) {
						if (op_input->type == XML_ELEMENT_NODE) {
							/* find position of this parameter */
							pos = 0;
							while (pos < ds->transapi.rpc_clbks->callbacks[i].arg_count) {
								if (xmlStrEqual(BAD_CAST ds->transapi.rpc_clbks->callbacks[i].arg_order[pos], op_input->name)) {
									/* store copy of node to position */
									((xmlNodePtr*)op_input_array)[pos] = xmlCopyNode(op_input, 1);
									break;
								}
								pos++;
							}
							/* input node with this name not found in model defined inputs of RPC */
							if (pos == ds->transapi.rpc_clbks->callbacks[i].arg_count) {
								WARN("%s: input parameter %s not defined for RPC %s",__func__, op_input->name, ds->transapi.rpc_clbks->callbacks[i].name);
							}
						}
						op_input = op_input->next;
					}

					/* call RPC callback function */
					VERB("Calling %s RPC function\n", rpc_name);
					reply = ds->transapi.rpc_clbks->callbacks[i].func(op_input_array);
					/* clean array */
					for (j=0; j<ds->transapi.rpc_clbks->callbacks[i].arg_count; j++) {
						xmlFreeNode(((xmlNodePtr*)op_input_array)[j]);
					}
					free (op_input_array);
					/* end RPC search, there can be only one RPC with name == op_name */
					break;
				}
			}
		}

		free(op_name);
		break;
	default:
		ERROR("%s: unsupported NETCONF operation requested.", __func__);
		return (nc_reply_error (nc_err_new (NC_ERR_OP_NOT_SUPPORTED)));
		break;
	}

	/* if reply was not already created */
	if (reply == NULL) {
		if (e != NULL) {
			/* operation failed and error is filled */
			reply = nc_reply_error(e);
		} else if (data == NULL && ret != EXIT_SUCCESS) {
			if (ret == EXIT_RPC_NOT_APPLICABLE) {
				/* operation can not be performed on this datastore */
				reply = NCDS_RPC_NOT_APPLICABLE;
			} else {
				/* operation failed, but no additional information is provided */
				reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
			}
		} else {
			if (data != NULL) {
				reply = nc_reply_data(data);
				free(data);
			} else {
				reply = nc_reply_ok();
			}
		}
	}

	/* if transapi used, rpc affected running and succeeded get its actual content */
	/*
	 * skip transapi if <edit-config> was performed with test-option set
	 * to test-only value
	 */
	if (ds->transapi.module != NULL
		&& (op == NC_OP_COMMIT || op == NC_OP_COPYCONFIG || (op == NC_OP_EDITCONFIG && (nc_rpc_get_testopt(rpc) != NC_EDIT_TESTOPT_TEST))) &&
		(nc_rpc_get_target(rpc) == NC_DATASTORE_RUNNING && nc_reply_get_type(reply) == NC_REPLY_OK)) {

		if ((new_reply = ncds_apply_transapi(ds, session, old, nc_rpc_get_erropt(rpc), NULL)) != NULL) {
			nc_reply_free(reply);
			reply = new_reply;
		}
	}
	xmlFreeDoc (old);
	old = NULL;

	if (id == NCDS_INTERNAL_ID) {
		if (old_reply == NULL) {
			old_reply = reply;
		} else if (old_reply != NCDS_RPC_NOT_APPLICABLE || reply != NCDS_RPC_NOT_APPLICABLE){
			if ((new_reply = nc_reply_merge(2, old_reply, reply)) == NULL) {
				if (nc_reply_get_type(old_reply) == NC_REPLY_ERROR) {
					return (old_reply);
				} else if (nc_reply_get_type(reply) == NC_REPLY_ERROR) {
					return (reply);
				} else {
					return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
				}
			}
			old_reply = reply = new_reply;
		}
		dsid++;
		if (dsid < internal_ds_count) {
			e = NULL;
			reply = NULL;
			goto process_datastore;
		}
	}

	return (reply);
}

nc_reply* ncds_apply_rpc2all(struct nc_session* session, const nc_rpc* rpc, ncds_id* ids[])
{
	struct ncds_ds_list* ds, *ds_rollback;
	nc_reply *old_reply = NULL, *new_reply = NULL, *reply = NULL;
	int id_i = 0, transapi = 0;
	char *op_name, *op_namespace, *data;
	xmlDocPtr old;
	NC_OP op;
	NC_DATASTORE target;
	NC_EDIT_ERROPT_TYPE erropt = NC_EDIT_ERROPT_NOTSET;
	NC_RPC_TYPE req_type;

	if (rpc == NULL || session == NULL) {
		ERROR("%s: invalid parameter %s", __func__, (rpc==NULL)?"rpc":"session");
		return (NULL);
	}

	req_type = nc_rpc_get_type(rpc);
	if (nc_rpc_get_op(rpc) == NC_OP_EDITCONFIG) {
		erropt = nc_rpc_get_erropt(rpc);
	}

	/* check that we have a valid definition of the requested RPC */
	op_name = nc_rpc_get_op_name(rpc);
	op_namespace = nc_rpc_get_op_namespace(rpc);
	if (ncds_get_model_operation(op_name, op_namespace) == NULL) {
		/* rpc operation is not defined in any known module */
		ERROR("%s: unsupported NETCONF operation (%s) requested.", __func__, op_name);
		free(op_name);
		free(op_namespace);
		return (nc_reply_error(nc_err_new (NC_ERR_OP_NOT_SUPPORTED)));
	}
	free(op_namespace);
	free(op_name);

	if (ids != NULL) {
		*ids = ncds.datastores_ids;
	}

	
	for (ds = ncds.datastores; ds != NULL; ds = ds->next) {
		/* skip internal datastores */
		if (ds->datastore->id > 0 && ds->datastore->id < internal_ds_count) {
			continue;
		}

		/* apply RPC on a single datastore */
		reply = ncds_apply_rpc(ds->datastore->id, session, rpc);
		if (ids != NULL && reply != NCDS_RPC_NOT_APPLICABLE) {
			ncds.datastores_ids[id_i] = ds->datastore->id;
			id_i++;
			ncds.datastores_ids[id_i] = -1; /* terminating item */
		}

		/* merge results from the previous runs */
		if (old_reply == NULL) {
			old_reply = reply;
		} else if (old_reply != NCDS_RPC_NOT_APPLICABLE || reply != NCDS_RPC_NOT_APPLICABLE) {
			if ((new_reply = nc_reply_merge(2, old_reply, reply)) == NULL) {
				if (nc_reply_get_type(old_reply) == NC_REPLY_ERROR) {
					return (old_reply);
				} else if (nc_reply_get_type(reply) == NC_REPLY_ERROR) {
					return (reply);
				} else {
					return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
				}
			}
			old_reply = reply = new_reply;
		}

		if (reply != NCDS_RPC_NOT_APPLICABLE && nc_reply_get_type(reply) == NC_REPLY_ERROR) {
			if (req_type == NC_RPC_DATASTORE_WRITE) {
				if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
					return (reply);
				} else if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
					/* rollback previously changed datastores */
					/* do not skip internal datastores */
					op = nc_rpc_get_op(rpc);
					target = nc_rpc_get_target(rpc);
					for (ds_rollback = ncds.datastores; ds_rollback != ds; ds_rollback = ds_rollback->next) {
						if (ds_rollback->datastore->transapi.module != NULL
								&& (op == NC_OP_COMMIT || op == NC_OP_COPYCONFIG || (op == NC_OP_EDITCONFIG && (nc_rpc_get_testopt(rpc) != NC_EDIT_TESTOPT_TEST)))
								&& ( target == NC_DATASTORE_RUNNING)) {
							transapi = 1;
						} else {
							transapi = 0;
						}

						if (transapi) {
							/* remeber data for transAPI diff */
							data = ds_rollback->datastore->func.getconfig(ds_rollback->datastore, session, NC_DATASTORE_RUNNING, NULL);
							if (data == NULL || strcmp(data, "") == 0) {
								old = xmlNewDoc(BAD_CAST "1.0");
							} else {
								old = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
							}
							free(data);
						}

						ds_rollback->datastore->func.rollback(ds_rollback->datastore);

						/* transAPI rollback */
						if (transapi) {
							reply = ncds_apply_transapi(ds_rollback->datastore, session, old, erropt, reply);
							xmlFreeDoc(old);
						}

					}
					return (reply);
				} /* else if (erropt == NC_EDIT_ERROPT_CONT)
				   * just continue
				   */
			} else if (req_type == NC_RPC_DATASTORE_READ) {
				return (reply);
			}
		}
	}
	
	return (reply);
}

void ncds_break_locks(const struct nc_session* session)
{
	struct ncds_ds_list * ds;
	struct nc_err * e = NULL;
	/* maximum is 3 locks (one for every datastore type) */
	struct nc_session * sessions[3];
	const struct ncds_lockinfo * lockinfo;
	int number_sessions = 0, i, j;
	NC_DATASTORE ds_type[3] = {NC_DATASTORE_CANDIDATE, NC_DATASTORE_RUNNING, NC_DATASTORE_STARTUP};
	struct nc_cpblts * cpblts;

	if (session == NULL) {
		/* if session NULL, get all sessions that hold lock from first file datastore */
		ds = ncds.datastores;
		/* find first file datastore */
		while (ds != NULL && ds->datastore != NULL && ds->datastore->type != NCDS_TYPE_FILE) {
			ds = ds->next;
		}
		if (ds != NULL) {
			/* if there is one */
			/* get default capabilities for dummy sessions */
			cpblts = nc_session_get_cpblts_default();
			for (i=0; i<3; i++) {
				if ((lockinfo = ncds_file_lockinfo (ds->datastore, ds_type[i])) != NULL) {
					if (lockinfo->sid != NULL && strcmp (lockinfo->sid, "") != 0) {
						/* create dummy session with session ID used to lock datastore */
						sessions[number_sessions++] = nc_session_dummy(lockinfo->sid, "dummy", NULL, cpblts);
					}
				}
			}
			nc_cpblts_free(cpblts);
		}
	} else {
		/* plain old single session break locks */
		number_sessions = 1;
		sessions[0] = (struct nc_session*)session;
	}

	/* for all prepared sessions */
	for (i=0; i<number_sessions; i++) {
		ds = ncds.datastores;
		/* every datastore */
		while (ds) {
			if (ds->datastore) {
				/* and every datastore type */
				for (j=0; j<3; j++) {
					/* try to unlock datastore */
					ds->datastore->func.unlock(ds->datastore, sessions[i], ds_type[j], &e);
					if (e) {
						nc_err_free(e);
						e = NULL;
					}
				}
			}
			ds = ds->next;
		}
	}

	/* clean created dummy sessions */
	if (session == NULL) {
		for (i=0; i<number_sessions; i++) {
			nc_session_free(sessions[i]);
		}
	}

	return;
}

const struct data_model* ncds_get_model_data(const char* namespace)
{
	struct model_list* listitem;
	struct data_model *model = NULL;

	if (namespace == NULL) {
		return (NULL);
	}

	for (listitem = models_list; listitem != NULL; listitem = listitem->next) {
		if (listitem->model->namespace != NULL && strcmp(listitem->model->namespace, namespace) == 0) {
			/* namespace matches */
			model = listitem->model;
			break;
		}
	}

	if (model != NULL) {
		/* model found */
		return (model);
	}

	/* model not found */
	return (NULL);
}

const struct data_model* ncds_get_model_operation(const char* operation, const char* namespace)
{
	const struct data_model *model = NULL;
	int i;

	if (operation == NULL || namespace == NULL) {
		return (NULL);
	}

	model = ncds_get_model_data(namespace);
	if (model != NULL && model->rpcs != NULL ) {
		for (i = 0; model->rpcs[i] != NULL ; i++) {
			if (strcmp(model->rpcs[i], operation) == 0) {
				/* operation definition found */
				return (model);
			}
		}
	}

	/* oepration definition not found */
	return (NULL);
}

const struct data_model* ncds_get_model_notification(const char* notification, const char* namespace)
{
	const struct data_model *model = NULL;
	int i;

	if (notification == NULL || namespace == NULL) {
		return (NULL);
	}

	model = ncds_get_model_data(namespace);
	if (model != NULL && model->notifs != NULL ) {
		for (i = 0; model->notifs[i] != NULL ; i++) {
			if (strcmp(model->notifs[i], notification) == 0) {
				/* notification definition found */
				return (model);
			}
		}
	}

	/* notification definition not found */
	return (NULL);
}
