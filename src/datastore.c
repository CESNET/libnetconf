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
#include <assert.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "netconf_internal.h"
#include "messages.h"
#include "error.h"
#include "with_defaults.h"
#include "notifications.h"
#include "session.h"
#include "datastore.h"
#include "datastore/edit_config.h"
#include "datastore/datastore_internal.h"
#include "datastore/file/datastore_file.h"
#include "datastore/empty/datastore_empty.h"

extern struct nc_statistics *nc_stats;

struct ncds_ds_list
{
	struct ncds_ds *datastore;
	struct ncds_ds_list* next;
};

/**
 * @brief Internal list of initiated datastores.
 */
static struct ncds_ds_list *datastores = NULL;

/**
 * @brief Get ncds_ds structure from datastore list containing storage
 * information with specified ID.
 *
 * @param[in] id ID of the storage.
 * @return Pointer to the required ncds_ds structure inside internal
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

	if (ds_iter == NULL) {
		return NULL;
	}

	return (ds_iter->datastore);
}

/**
 * @brief Remove datastore with specified ID from the internal datastore list.
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
		free(ds_iter);
	}

	return retval;
}

char * ncds_get_model(ncds_id id)
{
	struct ncds_ds * datastore = datastores_get_ds(id);
	xmlBufferPtr buf;
	char * retval = NULL;

	if (datastore != NULL && datastore->model != NULL) {
		buf = xmlBufferCreate();
		xmlNodeDump(buf, datastore->model, datastore->model->children, 1, 1);
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

	return datastore->model_path;
}

int get_model_info(xmlDocPtr model, char **name, char **version, char **namespace)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr result = NULL;
	xmlChar *xml_aux;
	int i, j, l;

	/* prepare xpath evaluation context of the model for XPath */
	if ((model_ctxt = xmlXPathNewContext(model)) == NULL) {
		ERROR("%s: Creating XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST "yin", BAD_CAST "urn:ietf:params:xml:ns:yang:yin:1") != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}

	/* get name of the schema */
	if (name != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				xmlXPathFreeContext (model_ctxt);
				return (EXIT_FAILURE);
			} else {
				*name = (char*) xmlGetProp (result->nodesetval->nodeTab[0], BAD_CAST "name");
			}
			xmlXPathFreeObject (result);
			if (*name == NULL ) {
				xmlXPathFreeContext (model_ctxt);
				return (EXIT_FAILURE);
			}
		}
	}

	/* get version */
	if (version != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:revision", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				xmlXPathFreeContext (model_ctxt);
				if (name != NULL) {
					xmlFree (*name);
					*name = NULL;
				}
				return (EXIT_FAILURE);
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
						for (j = 0; j < l; j++) {
							if (xml_aux[j] > *version[j]) {
								free (*version);
								*version = (char*)xml_aux;
								xml_aux = NULL;
								break;
							} else if (xml_aux[j] < *version[j]) {
								break;
							}
						}
						free (xml_aux);
					}
				}
			}
			xmlXPathFreeObject (result);
			if (*version == NULL ) {
				xmlXPathFreeContext (model_ctxt);
				if (name != NULL) {
					xmlFree (*name);
					*name = NULL;
				}
				return (EXIT_FAILURE);
			}
		}
	}

	/* get namespace of the schema */
	if (namespace != NULL ) {
		result = xmlXPathEvalExpression (BAD_CAST "/yin:module/yin:namespace", model_ctxt);
		if (result != NULL ) {
			if (result->nodesetval->nodeNr < 1) {
				xmlXPathFreeContext (model_ctxt);
				if (name != NULL ) {
					xmlFree (*name);
					*name = NULL;
				}
				if (version != NULL ) {
					xmlFree (*version);
					*version = NULL;
				}
				return (EXIT_FAILURE);
			} else {
				*namespace = (char*) xmlGetProp (result->nodesetval->nodeTab[0], BAD_CAST "uri");
			}
			xmlXPathFreeObject (result);
			if (*namespace == NULL ) {
				xmlXPathFreeContext (model_ctxt);
				if (name != NULL ) {
					xmlFree (*name);
					*name = NULL;
				}
				if (version != NULL ) {
					xmlFree (*version);
					*version = NULL;
				}
				return (EXIT_FAILURE);
			}
		}
	}

	xmlXPathFreeContext(model_ctxt);

	return (EXIT_SUCCESS);
}

char* get_schemas()
{
	char *schema_name, *version = NULL, *namespace = NULL;
	struct ncds_ds_list* ds = NULL;
	char *schema = NULL, *schemas = NULL, *aux = NULL;

	for (ds = datastores; ds != NULL ; ds = ds->next) {
		if (ds->datastore->model == NULL) {
			continue;
		}

		if (get_model_info(ds->datastore->model, &schema_name, &version, &namespace) != 0) {
			continue;
		}
		aux = NULL;
		asprintf(&aux,"<schema><identifier>%s</identifier>"
				"<version>%s</version>"
				"<format>yin</format>"
				"<namespace>%s</namespace>"
				"<location>NETCONF</location>"
				"</schema>",
				schema_name, version, namespace);
		free(schema_name);
		free(version);
		free(namespace);

		if (schema == NULL) {
			schema = aux;
		} else {
			schema = realloc(schema, strlen(schema) + strlen(aux) + 1);
			strcat(schema, aux);
		}
	}

	if (schema != NULL) {
		asprintf(&schemas, "<schemas>%s</schemas>", schema);
		free(schema);
	}
	return (schemas);
}

char* get_internal_state(const struct nc_session *session)
{
	char *notifs = NULL, *schemas = NULL, *sessions = NULL, *retval = NULL, *ds_stats = NULL, *ds_startup = NULL, *ds_cand = NULL, *stats = NULL, *aux = NULL;
	xmlDocPtr doc;
	xmlBufferPtr buf;
	struct ncds_ds_list* ds = NULL;
	const struct ncds_lockinfo *info;

	/*
	 * capabilities
	 */
	if (nc_cpblts_enabled (session, NC_CAP_NOTIFICATION_ID)) {
		notifs = ncntf_status ();
		doc = xmlReadDoc (BAD_CAST notifs, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		free (notifs);
		if (doc == NULL ) {
			notifs = NULL;
		} else {
			buf = xmlBufferCreate ();
			xmlNodeDump (buf, doc, doc->children, 1, 1);
			notifs = strdup ((char*) xmlBufferContent (buf));
			xmlFreeDoc (doc);
			xmlBufferFree (buf);
		}
	}

	/*
	 * datastores
	 */
	/* find non-empty datastore implementation */
	for (ds = datastores; ds != NULL ; ds = ds->next) {
		if (ds->datastore && ds->datastore->type == NCDS_TYPE_FILE) {
			break;
		}
	}

	if (ds != NULL ) {
		if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) == 1) {
			info = ds->datastore->func.get_lockinfo (ds->datastore, NC_DATASTORE_STARTUP);
			if (info != NULL && info->sid != NULL ) {
				asprintf (&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
						"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time);
			}
			asprintf (&ds_startup, "<datastore><name>startup</name>%s</datastore>",
			        (aux != NULL )? aux : "");
			free (aux);
			aux = NULL;
		}
		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) == 1) {
			info = ds->datastore->func.get_lockinfo (ds->datastore, NC_DATASTORE_CANDIDATE);
			if (info != NULL && info->sid != NULL ) {
				asprintf (&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
						"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time);
			}
			asprintf (&ds_cand, "<datastore><name>candidate</name>%s</datastore>",
			        (aux != NULL )? aux : "");
			free (aux);
			aux = NULL;
		}
		info = ds->datastore->func.get_lockinfo (ds->datastore, NC_DATASTORE_RUNNING);
		if (info != NULL && info->sid != NULL ) {
			asprintf (&aux, "<locks><global-lock><locked-by-session>%s</locked-by-session>"
					"<locked-time>%s</locked-time></global-lock></locks>", info->sid, info->time);
		}
		asprintf (&ds_stats, "<datastores><datastore><name>running</name>%s</datastore>%s%s</datastores>",
		        (aux != NULL )? aux : "",
		        (ds_startup != NULL) ? ds_startup : "",
		        (ds_cand != NULL) ? ds_cand : "");
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
	if (nc_stats != NULL) {
		asprintf(&stats, "<statistics><netconf-start-time>%s</netconf-start-time>"
				"<in-bad-hellos>%u</in-bad-hellos>"
				"<in-sessions>%u</in-sessions>"
				"<dropped-sessions>%u</dropped-sessions>"
				"<in-rpcs>%u</in-rpcs>"
				"<in-bad-rpcs>%u</in-bad-rpcs>"
				"<out-rpc-errors>%u</out-rpc-errors>"
				"<out-notifications>%u</out-notifications></statistics>",
				nc_stats->start_time,
				nc_stats->bad_hellos,
				nc_stats->sessions_in,
				nc_stats->sessions_dropped,
				nc_stats->counters.in_rpcs,
				nc_stats->counters.in_bad_rpcs,
				nc_stats->counters.out_rpc_errors,
				nc_stats->counters.out_notifications);
	}

	/* get it all together */
	asprintf(&retval, "<netconf-state xmlns=\"%s\">%s%s%s%s%s</netconf-state>%s", NC_NS_CAP_MONITORING,
			(session->capabilities_original != NULL) ? session->capabilities_original : "",
			(ds_stats != NULL) ? ds_stats : "",
			(sessions != NULL) ? sessions : "",
			(schemas != NULL) ? schemas : "",
			(stats != NULL) ? stats : "",
			(notifs != NULL) ? notifs : "");
	if (retval == NULL) {
		retval = strdup("");
	}

	free(ds_stats);
	free(sessions);
	free(stats);
	free(notifs);

	return (retval);
}

char* get_schema(const nc_rpc* rpc, struct nc_err** e)
{
	struct ncds_ds_list* ds = NULL;
	xmlNodePtr node;
	char *name = NULL, *aux_name = NULL, *version = NULL, *aux_version = NULL, *format = NULL;
	char *retval = NULL;
	xmlBufferPtr resultbuffer;

	if (rpc == NULL ||
			rpc->doc == NULL ||
			rpc->doc->children == NULL ||
			rpc->doc->children->children == NULL ) {
		return (NULL);
	}

	for (node = rpc->doc->children->children->children; node != NULL; node = node->next) {
		if (xmlStrcmp(node->name, BAD_CAST "identifier") == 0) {
			name = (char*) xmlNodeGetContent(node);
		} else if(xmlStrcmp(node->name, BAD_CAST "version") == 0) {
			version = (char*) xmlNodeGetContent(node);
		} else if(xmlStrcmp(node->name, BAD_CAST "format") == 0) {
			format = (char*) xmlNodeGetContent(node);
			/* only yin format is supported now */
			if (strcmp(format, "yin") != 0) {
				if (e != NULL) {
					*e = nc_err_new(NC_ERR_INVALID_VALUE);
					nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "format");
				}
				free(format);
				free(version);
				free(name);
				return(NULL);
			}
		}
	}

	if (name == NULL) {
		if (e != NULL) {
			*e = nc_err_new(NC_ERR_INVALID_VALUE);
			nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "identifier");
		}
		free(format);
		free(version);
		free(name);
		return(NULL);
	}

	for (ds = datastores; ds != NULL ; ds = ds->next) {
		if (ds->datastore->model == NULL) {
			continue;
		}

		if (get_model_info(ds->datastore->model, &aux_name, &aux_version, NULL) != 0) {
			continue;
		}

		if (strcmp(name, aux_name) == 0) {
			if (version == NULL || strcmp(version, aux_version) == 0) {
				/* check for uniqness */
				if (retval != NULL) {
					free(retval);
					free(format);
					free(version);
					free(name);
					if (e != NULL) {
						*e = nc_err_new(NC_ERR_OP_FAILED);
						nc_err_set(*e, NC_ERR_PARAM_APPTAG, "data-not-unique");
					}
					return(NULL);
				}
				/* got the required model, dump it */

				resultbuffer = xmlBufferCreate();
				if (resultbuffer == NULL) {
					ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
					return NULL;
				}
				xmlNodeDump(resultbuffer, ds->datastore->model, ds->datastore->model->children, 2, 1);
				retval = strdup((char *) xmlBufferContent(resultbuffer));
				xmlBufferFree(resultbuffer);
			}
		}
	}
	return (retval);
}

struct ncds_ds* ncds_new(NCDS_TYPE type, const char* model_path, char* (*get_state)(const char* model, const char* running, struct nc_err** e))
{
	struct ncds_ds* ds = NULL;

	if (model_path == NULL) {
		ERROR("%s: missing model path parameter.", __func__);
		return (NULL);
	}

	switch (type) {
	case NCDS_TYPE_FILE:
		ds = (struct ncds_ds*) calloc(1, sizeof(struct ncds_ds_file));
		ds->func.init = ncds_file_init;
		ds->func.free = ncds_file_free;
		ds->func.get_lockinfo = ncds_file_lockinfo;
		ds->func.lock = ncds_file_lock;
		ds->func.unlock = ncds_file_unlock;
		ds->func.getconfig = ncds_file_getconfig;
		ds->func.copyconfig = ncds_file_copyconfig;
		ds->func.deleteconfig = ncds_file_deleteconfig;
		ds->func.editconfig = ncds_file_editconfig;
		break;
	case NCDS_TYPE_EMPTY:
		ds = (struct ncds_ds*) calloc(1, sizeof(struct ncds_ds_empty));
		ds->func.init = ncds_empty_init;
		ds->func.free = ncds_empty_free;
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
	ds->get_state = get_state;

	/* ds->id stays 0 to indicate, that datastore is still not fully configured */

	return (ds);
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

ncds_id ncds_init(struct ncds_ds* datastore)
{
	struct ncds_ds_list * item;

	if (datastore == NULL) {
		return -1;
	}

	/** \todo data model validation */

	/* call implementation-specific datastore init() function */
	if (datastore->func.init(datastore) != 0) {
		return -2;
	}

	/* acquire unique id */
	datastore->id = generate_id();

	/* add to list */
	item = malloc(sizeof(struct ncds_ds_list));
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
		WARN("%s: no datastore to free.", __func__);
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

void ncds_free2(ncds_id datastore_id)
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
	ret = edit_merge(result, second->children, keys);

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
 * \brief compare node properties against reference node properties
 *
 * \param reference     reference node, compared node must has got all
 *                      properties (and same values) as reference node
 * \param node          compared node
 *
 * \return              0 if compared node contain all properties (with same
 *                      values) as reference node, 1 otherelse
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
 * \return              1 if config is filter output, 0 otherelse
 */

static int ncxml_subtree_filter(xmlNodePtr config, xmlNodePtr filter)
{
	xmlNodePtr config_node = config;
	xmlNodePtr filter_node = filter;
	xmlNodePtr delete = NULL, delete2 = NULL;

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
			/* choose kind of used filter node */
			if (config_node->children && (config_node->children->type == XML_TEXT_NODE) &&
					(strcmp((char *) filter_node->children->content, (char *) config_node->children->content))) {
				/* content match node doesn't match */
				return 0;
			}
			if (filter_node->next || filter_node->prev) {
				/* check if all filter sibling nodes are content match nodes -> then no config sibling node will be removed */
				sibling_selection = 0; /* 0 means that all sibling nodes will be in the filter result */
				/*go to the first filter sibling node */
				filter_node = filter;
				/* pass all filter sibling nodes */
				while (filter_node) {
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
								if (strcmp((char *) filter_node->children->content, (char *) config_node->children->content)) {
									/* content match node doesn't match */
									return 0;
								}
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

int ncxml_filter(xmlDocPtr data, const struct nc_filter * filter)
{
	xmlDocPtr filter_doc, result, data_filtered[2] = {NULL, NULL};
	xmlNodePtr filter_item, node;
	char* filter_text = NULL;
	int ret = EXIT_FAILURE;

	if (data == NULL || data->children == NULL || filter == NULL) {
		return EXIT_SUCCESS;
	}

	switch (filter->type) {
	case NC_FILTER_SUBTREE:
		asprintf(&filter_text, "<filter>%s</filter>", filter->content);
		if ((filter_doc = xmlReadDoc(BAD_CAST filter_text, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN)) == NULL) {
			return EXIT_FAILURE;
		}
		for (filter_item = filter_doc->children->children; filter_item != NULL; filter_item = filter_item->next) {
			data_filtered[0] = xmlCopyDoc(data, 1);
			ncxml_subtree_filter(data_filtered[0]->children, filter_item);
			if (data_filtered[1] == NULL) {
				if (data_filtered[0]->children == NULL) {
					/* empty result */
					xmlFreeDoc(data_filtered[0]);
					data_filtered[0] = NULL;
				}
				data_filtered[1] = data_filtered[0];
			} else {
				result = ncxml_merge(data_filtered[0], data_filtered[1], NULL);
				xmlFreeDoc(data_filtered[0]);
				xmlFreeDoc(data_filtered[1]);
				data_filtered[1] = result;
			}
		}
		if (filter_doc->children->children != NULL) {
			/* we have filtered at least one time */
			node = data->children;
			xmlUnlinkNode(node);
			xmlFreeNode(node);
			if (data_filtered[1] != NULL) {
				xmlDocSetRootElement(data, xmlCopyNode(data_filtered[1]->children, 1));
				xmlFreeDoc(data_filtered[1]);
			}
		} /* else empty filter -> original data doc is unchanged */

		xmlFreeDoc(filter_doc);
		ret = EXIT_SUCCESS;
		break;
	default:
		ret = EXIT_FAILURE;
		break;
	}

	return ret;
}

/**
 * \brief Get appropriate root node from edit-config's \<config\> element according to the specified data model
 *
 * \param[in] roots First of the root elements in edit-config's \<config\>
 *                  (first children of this element).
 * \param[in] model XML form (YIN) of the configuration data model.
 *
 * \return Root element matching specified configuration data model.
 */
xmlNodePtr get_model_root(xmlNodePtr roots, xmlDocPtr model)
{
	xmlNodePtr retval, aux;
	xmlChar *root_name = NULL, *ns = NULL;

	assert(roots != NULL);
	assert(model != NULL);

	if (model->children == NULL || xmlStrcmp(model->children->name, BAD_CAST "module") != 0) {
		return NULL;
	}

	aux = model->children->children;
	while (aux != NULL) {
		if (root_name == NULL && xmlStrcmp(aux->name, BAD_CAST "container") == 0) {
			//root_name = xmlGetNsProp(aux, BAD_CAST "name", BAD_CAST NC_NS_YIN);
			root_name = xmlGetProp(aux, BAD_CAST "name");
			if (ns != NULL) {
				break;
			}
		} else if (ns == NULL && xmlStrcmp(aux->name, BAD_CAST "namespace") == 0) {
			//ns = xmlGetNsProp(aux, BAD_CAST "uri", BAD_CAST NC_NS_YIN);
			ns = xmlGetProp(aux, BAD_CAST "uri");
			if (root_name != NULL) {
				break;
			}
		}
		aux = aux->next;
	}
	if (root_name == NULL || ns == NULL) {
		ERROR("Invalid configuration data model - root container or namespace missing (%s:%d).", __FILE__, __LINE__);
		return NULL;
	}

	retval = roots;
	while (retval != NULL) {
		if (xmlStrcmp(retval->name, root_name) == 0 && (retval->ns == NULL || xmlStrcmp(retval->ns->href, ns) == 0)) {
			break;
		}

		retval = retval->next;
	}
	xmlFree(ns);
	xmlFree(root_name);

	return retval;
}

nc_reply* ncds_apply_rpc(ncds_id id, const struct nc_session* session, const nc_rpc* rpc)
{
	struct nc_err* e = NULL;
	struct ncds_ds* ds;
	struct nc_filter * filter;
	char* data = NULL, *config, *model = NULL, *data2;
	xmlDocPtr doc1, doc2, doc_merged;
	int len;
	int ret = EXIT_FAILURE;
	nc_reply* reply;
	xmlBufferPtr resultbuffer;
	xmlNodePtr aux_node;
	NC_OP op;
	NC_DATASTORE source_ds, target_ds;

	if (rpc->type.rpc != NC_RPC_DATASTORE_READ && rpc->type.rpc != NC_RPC_DATASTORE_WRITE) {
		return (nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED)));
	}

	/* request for internal datastore */
	if (id == 0) {
		switch (op = nc_rpc_get_op(rpc)) {
		case NC_OP_LOCK:
		case NC_OP_UNLOCK:
		case NC_OP_COPYCONFIG:
		case NC_OP_EDITCONFIG:
		case NC_OP_DELETECONFIG:
			reply = nc_reply_ok();
			break;
		case NC_OP_COMMIT:
		case NC_OP_DISCARDCHANGES:
			if (nc_cpblts_enabled(session, NC_CAP_CANDIDATE_ID)) {
				reply = nc_reply_ok();
			} else {
				reply = nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED));
			}
			break;
		case NC_OP_GET:
			data = get_internal_state(session);
			if (data != NULL) {
				reply = nc_reply_data(data);
				free(data);
			} else {
				reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
			}
			break;
		case NC_OP_GETCONFIG:
			reply = nc_reply_data("");
			break;
		case NC_OP_GETSCHEMA:
			if (nc_cpblts_enabled (session, NC_CAP_MONITORING_ID)) {
				data = get_schema (rpc, &e);

				if (e != NULL ) {
					/* operation failed and error is filled */
					reply = nc_reply_error (e);
				} else if (data == NULL ) {
					/* operation failed, but no additional information is provided */
					reply = nc_reply_error (nc_err_new (NC_ERR_OP_FAILED));
				} else {
					reply = nc_reply_data (data);
					free (data);
				}
			} else {
				reply = nc_reply_error (nc_err_new (NC_ERR_OP_NOT_SUPPORTED));
			}
			break;
		default:
			return (nc_reply_error (nc_err_new (NC_ERR_OP_NOT_SUPPORTED)));
			break;
		}
		return (reply);
	}

	ds = datastores_get_ds(id);
	if (ds == NULL) {
		return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
	}

	switch (op = nc_rpc_get_op(rpc)) {
	case NC_OP_LOCK:
		ret = ds->func.lock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_UNLOCK:
		ret = ds->func.unlock(ds, session, nc_rpc_get_target(rpc), &e);
		break;
	case NC_OP_GET:
		data = ds->func.getconfig(ds, session, NC_DATASTORE_RUNNING, &e);

		if (ds->get_state != NULL) {
			/* caller provided callback function to retrieve status data */

			xmlDocDumpMemory(ds->model, (xmlChar**) (&model), &len);
			data2 = ds->get_state(model, data, &e);
			free(model);

			if (e != NULL) {
				/* state data retrival error */
				free(data);
				break;
			}

			/* merge status and config data */
			doc1 = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			doc2 = xmlReadDoc(BAD_CAST data2, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			free(data2);
			/* if merge fail (probably one of docs NULL)*/
			if ((doc_merged = ncxml_merge(doc1, doc2, ds->model)) == NULL) {
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
			doc_merged = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		}
		free(data);

		if (doc_merged == NULL) {
			ERROR("Reading configuration datastore failed.");
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid datastore content.");
			break;
		}

		/* process default values */
		ncdflt_default_values(doc_merged, ds->model, ncdflt_rpc_get_withdefaults(rpc));

		/* if filter specified, now is good time to apply it */
		if ((filter = nc_rpc_get_filter(rpc)) != NULL) {
			if (ncxml_filter(doc_merged, filter) != 0) {
				ERROR("Filter failed.");
				e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
				nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "filter");
			}
		}
		nc_filter_free(filter);

		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			e = nc_err_new(NC_ERR_OP_FAILED);
			break;
		}
		for (aux_node = doc_merged->children; aux_node != NULL; aux_node = aux_node->next) {
			xmlNodeDump(resultbuffer, doc_merged, aux_node, 2, 1);
		}
		data = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(doc_merged);

		break;
	case NC_OP_GETCONFIG:
		if ((data = ds->func.getconfig(ds, session, nc_rpc_get_source(rpc), &e)) == NULL) {
			if (e == NULL) {
				ERROR ("%s: Failed to get data from datastore (%s:%d).", __func__, __FILE__, __LINE__);
				e = nc_err_new(NC_ERR_OP_FAILED);
			}
			break;
		}
		if (strcmp(data, "") == 0) {
			doc_merged = xmlNewDoc(BAD_CAST "1.0");
		} else {
			doc_merged = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		}
		free(data);

		if (doc_merged == NULL) {
			ERROR("Reading configuration datastore failed.");
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Invalid datastore content.");
			break;
		}

		/* process default values */
		ncdflt_default_values(doc_merged, ds->model, ncdflt_rpc_get_withdefaults(rpc));

		/* if filter specified, now is good time to apply it */
		if ((filter = nc_rpc_get_filter(rpc)) != NULL) {
			if (ncxml_filter(doc_merged, filter) != 0) {
				ERROR("Filter failed.");
				e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
				nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "filter");
			}
		}
		nc_filter_free(filter);

		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			e = nc_err_new(NC_ERR_OP_FAILED);
			break;
		}
		for (aux_node = doc_merged->children; aux_node != NULL; aux_node = aux_node->next) {
			xmlNodeDump(resultbuffer, doc_merged, aux_node, 2, 1);
		}
		data = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(doc_merged);

		break;
	case NC_OP_EDITCONFIG:
	case NC_OP_COPYCONFIG:
		/* check target element */
		if ((target_ds = nc_rpc_get_target(rpc)) == NC_DATASTORE_ERROR) {
			e = nc_err_new(NC_ERR_BAD_ELEM);
			nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "target");
			break;
		}

		if (op == NC_OP_COPYCONFIG && ((source_ds = nc_rpc_get_source(rpc)) != NC_DATASTORE_CONFIG)) {
			if (source_ds == NC_DATASTORE_ERROR) {
				e = nc_err_new(NC_ERR_BAD_ELEM);
				nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "source");
				break;
			}
			/* <copy-config> with specified source datastore */
			if (target_ds == source_ds) {
				e = nc_err_new(NC_ERR_INVALID_VALUE);
				nc_err_set(e, NC_ERR_PARAM_MSG, "Both target and source identify the same datastore.");
				break;
			}
			config = NULL;
		} else {
			/*
			 * config can contain multiple elements on the root level, so
			 * cover it with the <config> element to allow creation of xml
			 * document
			 */
			config = nc_rpc_get_config(rpc);
			if (strcmp(config, "") == 0) {
				/* config is empty -> ignore rest of magic here,
				 * go to application of the operation and do
				 * delete of the datastore (including running)!
				 */
				goto apply_editcopyconfig;
			}

			asprintf(&data, "<config>%s</config>", config);
			free(config);
			doc1 = xmlReadDoc(BAD_CAST data, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
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
			aux_node = get_model_root(doc1->children->children, ds->model);
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
				ret = EXIT_SUCCESS;
				data = NULL;
				break;
			}

			/* do some work in case of used with-defaults capability */
			if ((session->wd_modes & NCDFLT_MODE_ALL_TAGGED) != 0) {
				/* if report-all-tagged mode is supported, 'default'
				 * attribute with 'true' or '1' value can appear and we
				 * have to check that the element's value is equal to
				 * default value. If does, the element is removed and
				 * it is supposed to be default, otherwise the
				 * invalid-value error reply must be returned.
				 */
				doc1 = xmlReadDoc(BAD_CAST config, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
				free(config);

				if (ncdflt_default_clear(doc1, ds->model) != EXIT_SUCCESS) {
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
			ret = ds->func.editconfig(ds, session, target_ds, config, nc_rpc_get_defop(rpc), nc_rpc_get_erropt(rpc), &e);
		} else if (op == NC_OP_COPYCONFIG) {
			ret = ds->func.copyconfig(ds, session, target_ds, source_ds, config, &e);
		} else {
			ret = EXIT_FAILURE;
		}
		free(config);

		/* log the event */
		if (ret == EXIT_SUCCESS && (target_ds == NC_DATASTORE_RUNNING || target_ds == NC_DATASTORE_STARTUP)) {
			ncntf_event_new(-1, NCNTF_BASE_CFG_CHANGE, target_ds, NCNTF_EVENT_BY_USER, session);
		}

		break;
	case NC_OP_DELETECONFIG:
		if (nc_rpc_get_target(rpc) == NC_DATASTORE_RUNNING) {
			/* can not delete running */
			e = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(e, NC_ERR_PARAM_MSG, "Can not delete running datastore.");
			break;
		}
		ret = ds->func.deleteconfig(ds, session, target_ds = nc_rpc_get_target(rpc), &e);

		/* log the event */
		if (ret == EXIT_SUCCESS && (target_ds == NC_DATASTORE_RUNNING || target_ds == NC_DATASTORE_STARTUP)) {
			ncntf_event_new(-1, NCNTF_BASE_CFG_CHANGE, target_ds, NCNTF_EVENT_BY_USER, session);
		}
		break;
	case NC_OP_COMMIT:
		/* \todo check somehow, that candidate is not locked by another session */

		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			ret = ds->func.copyconfig (ds, session, NC_DATASTORE_RUNNING, NC_DATASTORE_CANDIDATE, NULL, &e);

			/* log the event */
			if (ret == EXIT_SUCCESS) {
				ncntf_event_new (-1, NCNTF_BASE_CFG_CHANGE, NC_DATASTORE_RUNNING, NCNTF_EVENT_BY_USER, session);
			}
		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
		break;
	case NC_OP_DISCARDCHANGES:
		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			ret = ds->func.copyconfig(ds, session, NC_DATASTORE_CANDIDATE, NC_DATASTORE_RUNNING, NULL, &e);
		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
		break;
	case NC_OP_GETSCHEMA:
		if (nc_cpblts_enabled (session, NC_CAP_MONITORING_ID)) {
			data = strdup("");
			ret = EXIT_SUCCESS;
		} else {
			e = nc_err_new (NC_ERR_OP_NOT_SUPPORTED);
			ret = EXIT_FAILURE;
		}
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

void ncds_break_locks(const struct nc_session* session)
{
	struct ncds_ds_list * ds;
	struct nc_err * e = NULL;

	ds = datastores;

	while (ds) {
		if (ds->datastore) {
			ds->datastore->func.unlock(ds->datastore, session, NC_DATASTORE_CANDIDATE, &e);
			if (e) {
				nc_err_free(e);
				e = NULL;
			}

			ds->datastore->func.unlock(ds->datastore, session, NC_DATASTORE_RUNNING, &e);
			if (e) {
				nc_err_free(e);
				e = NULL;
			}

			ds->datastore->func.unlock(ds->datastore, session, NC_DATASTORE_STARTUP, &e);
			if (e) {
				nc_err_free(e);
				e = NULL;
			}
		}
		ds = ds->next;
	}

	return;
}
