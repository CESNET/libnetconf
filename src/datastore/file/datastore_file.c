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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <libxml/tree.h>

#include "../../netconf_internal.h"
#include "../../error.h"
#include "../datastore_internal.h"
#include "datastore_file.h"

#define FILEDSFRAME "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<datastores xmlns=\"urn:cesnet:tmc:datastores:file\">\
  <running lock=\"\"/>\
  <startup lock=\"\"/>\
  <candidate lock=\"\"/>\
</datastores>"

int ncds_file_set_path (struct ncds_ds* datastore, char* path)
{
	struct ncds_ds_file * file_ds = (struct ncds_ds_file*)datastore;

	if (datastore == NULL) {
		ERROR ("Invalid datastore.");
		return -1;
	}

	if (path == NULL) {
		ERROR ("Invalid path.");
		return -2;
	}

	/* file does not exist */
	if (access (path, F_OK) != 0) {
		WARN ("Datastore file %s does not exist, creating it.", path);
		/* try to create it */
		file_ds->file = fopen (path, "w+");
		if (file_ds->file == NULL) {
			ERROR ("Datastore file %s can not be created (%s).", path, strerror(errno));
			return (-2);
		} else {
			VERB ("Datastore file %s was created.", path);
		}
	} else if (access (path, W_OK|R_OK)) {
		ERROR ("Insufficient rights for manipulation with the datastore file %s.", path);
		return (-2);
	} else {
		/* file exists and it is accessible */
		file_ds->file = fopen (path, "r+");
		if (file_ds->file == NULL) {
			ERROR ("Datastore file %s can not be opened (%s).", path, strerror(errno));
			return -2;
		}
	}
	file_ds->path = strdup(path);

	return 0;
}

/**
 * @brief Checks if structure of XML is matches the expected one
 * @param[in] doc Document to check.
 * @return non-zero if matches zero if not.
 */
static int file_structure_check (xmlDocPtr doc)
{
	xmlNodePtr root, ds;
	int running = 0, candidate = 0, startup = 0;

	root = xmlDocGetRootElement (doc);
	if (root == NULL ||!xmlStrEqual (root->name, BAD_CAST "datastores")) {
		return 0;
	}

	for (ds = root->children; ds != NULL; ds = ds->next) {
		if (ds->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (xmlStrEqual (ds->name, BAD_CAST "candidate")) {
			if (candidate) {
				ERROR ("Duplicate datastore candidate found.");
				return 0;
			} else {
				candidate = 1;
			}
		} else if (xmlStrEqual (ds->name, BAD_CAST "running")) {
			if (running) {
				ERROR ("Duplicate datastore running found.");
				return 0;
			} else {
				running = 1;
			}
		} else if (xmlStrEqual (ds->name, BAD_CAST "startup")) {
			if (startup) {
				ERROR ("Duplicate datastore startup found.");
				return 0;
			} else {
				startup = 1;
			}
		} else {
			VERB ("File datastore structure check: ignoring unknown element %s.", ds->name);
		}
	}

	if (candidate && running && startup) {
		return 1;
	}

	return 0;
}

/**
 * @brief Create xml frame of the file datastore
 * @return xml document holding basic structure
 */
static xmlDocPtr file_create_xmlframe ()
{
	xmlDocPtr doc;

	doc = xmlReadDoc(BAD_CAST FILEDSFRAME, NULL, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN);
	if (doc == NULL) {
		ERROR ("%s: creating empty file datastore failed.", __func__);
		return (NULL);
	}

	return doc;
}

static int file_fill_dsnodes(struct ncds_ds_file* ds)
{
	xmlNodePtr aux;

	if (ds == NULL || ds->xml == NULL || ds->xml->children == NULL) {
		ERROR("%s: invalid input parameter.", __func__);
		return (EXIT_FAILURE);
	}
	ds->running = NULL;
	ds->startup = NULL;
	ds->candidate = NULL;

	for (aux = ds->xml->children->children; aux != NULL; aux = aux->next) {
		if (xmlStrcmp(aux->name, BAD_CAST "running") == 0) {
			if (ds->running != NULL) {
				goto invalid_ds;
			} else {
				ds->running = aux;
			}
		}else if (xmlStrcmp(aux->name, BAD_CAST "startup") == 0) {
			if (ds->startup != NULL) {
				goto invalid_ds;
			} else {
				ds->startup = aux;
			}
		}else if (xmlStrcmp(aux->name, BAD_CAST "candidate") == 0) {
			if (ds->candidate != NULL) {
				goto invalid_ds;
			} else {
				ds->candidate = aux;
			}
		}
		/* else - ignore such unknown nodes until we get all required nodes */
	}

	if (ds->running == NULL || ds->startup == NULL || ds->candidate == NULL) {
		/* xml structure of the file datastore is invalid */
		goto invalid_ds;
	}

	return (EXIT_SUCCESS);

invalid_ds:
	ds->running = NULL;
	ds->startup = NULL;
	ds->candidate = NULL;
	return (EXIT_FAILURE);
}

/**
 * @ingroup store
 * @brief Initialization of file datastore
 *
 * @file_ds File datastore structure 
 *
 * @return 0 on success, non-zero else
 */
int ncds_file_init (struct ncds_ds* ds)
{
	struct stat st;
	char* new_path;
	int fd;
	struct ncds_ds_file* file_ds = (struct ncds_ds_file*)ds;

	file_ds->xml = xmlReadFile (file_ds->path, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN);
	if (file_ds->xml == NULL || file_structure_check (file_ds->xml) == 0) {
		WARN ("Failed to parse XML in file.");
		if (stat(file_ds->path, &st) || st.st_size > 0) {
			/* Unable to determine size or size bigger than 0 */
			WARN ("File %s contains some unknown data.", file_ds->path);

			/* cleanup so far structures because new will be created */
			fclose (file_ds->file);
			if (file_ds->xml != NULL) {
				xmlFreeDoc (file_ds->xml);
			}
			file_ds->file = NULL;
			file_ds->xml = NULL;

			/* Create file based on original name */
			asprintf (&new_path, "%s.XXXXXX", file_ds->path);
			fd = mkstemp (new_path);
			if (fd == -1 || (file_ds->file = fdopen(fd, "r+")) == NULL) {
				ERROR ("Can not create alternate file %s (%s).", new_path, strerror(errno));
				free (new_path);
				return (EXIT_FAILURE);
			}

			/* store new path */
			free (file_ds->path);
			file_ds->path = new_path;
			WARN("Using file %s to prevent data loss.", file_ds->path);
		}
		file_ds->xml = file_create_xmlframe();
		if (file_ds->xml == NULL) {
			return (EXIT_FAILURE);
		}
		xmlDocFormatDump(file_ds->file, file_ds->xml, 1);
		WARN ("File %s was empty. Basic structure created.", file_ds->path);
	}

	/* get pointers to running, startup and candidate nodes in xml */
	if (file_fill_dsnodes(file_ds) != EXIT_SUCCESS) {
		return (EXIT_FAILURE);
	}

	/* unlock forgotten locks if any */
	xmlSetProp (file_ds->running, BAD_CAST "lock", BAD_CAST "");
	xmlSetProp (file_ds->startup, BAD_CAST "lock", BAD_CAST "");
	xmlSetProp (file_ds->candidate, BAD_CAST "lock", BAD_CAST "");

	return EXIT_SUCCESS;
}

void ncds_file_free(struct ncds_ds* ds)
{
	struct ncds_ds_file* file_ds = (struct ncds_ds_file*)ds;

	if (file_ds != NULL) {
		/* generic ncds_ds part */
		if (file_ds->model_path != NULL) {
			free(file_ds->model_path);
		}
		if (file_ds->model != NULL) {
			xmlFreeDoc(file_ds->model);
		}
		/* ncds_ds_file specific part */
		if (file_ds->file != NULL) {
			fclose(file_ds->file);
		}
		if (file_ds->path != NULL) {
			free(file_ds->path);
		}
		if (file_ds->xml != NULL) {
			xmlFreeDoc(file_ds->xml);
		}
		free(file_ds);
	}
}

void ncds_file_sync(struct ncds_ds_file* file_ds)
{
	xmlDocFormatDump(file_ds->file, file_ds->xml, 1);
}

int ncds_file_lock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	struct ncds_ds_file* file_ds = (struct ncds_ds_file*)ds;
	xmlChar* lock;
	xmlNodePtr target_ds;
	int retval = EXIT_SUCCESS;

	/* check validity of function parameters */
	switch(target) {
	case NC_DATASTORE_RUNNING:
		target_ds = file_ds->running;
		break;
	case NC_DATASTORE_STARTUP:
		target_ds = file_ds->startup;
		break;
	case NC_DATASTORE_CANDIDATE:
		target_ds = file_ds->candidate;
		break;
	default:
		ERROR("%s: invalid target.", __func__);
		*error = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
		return (EXIT_FAILURE);
		break;
	}

	/* check if repository is locked */
	lock = xmlGetProp(target_ds, BAD_CAST "lock");
	if (lock != NULL) {
		if (xmlStrcmp(lock, BAD_CAST "") == 0) {
			/* datastore is NOT locked */
			xmlSetProp (target_ds, BAD_CAST "lock", BAD_CAST session->session_id);
		} else {
			/* datastore is locked */
			*error = nc_err_new(NC_ERR_LOCK_DENIED);
			nc_err_set(*error, NC_ERR_PARAM_INFO_SID, (char*)lock);
			retval = EXIT_FAILURE;
		}
		xmlFree(lock);
	}

	if (retval == EXIT_SUCCESS) {
		ncds_file_sync(file_ds);
	}

	return (retval);
}

int ncds_file_unlock (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	struct ncds_ds_file* file_ds = (struct ncds_ds_file*)ds;
	xmlChar* lock;
	xmlNodePtr target_ds;
	int retval = EXIT_SUCCESS;

	/* check validity of function parameters */
	switch(target) {
	case NC_DATASTORE_RUNNING:
		target_ds = file_ds->running;
		break;
	case NC_DATASTORE_STARTUP:
		target_ds = file_ds->startup;
		break;
	case NC_DATASTORE_CANDIDATE:
		target_ds = file_ds->candidate;
		break;
	default:
		ERROR("%s: invalid target.", __func__);
		*error = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
		return (EXIT_FAILURE);
		break;
	}

	/* check if repository is locked */
	lock = xmlGetProp(target_ds, BAD_CAST "lock");
	if (lock != NULL) {
		if (xmlStrcmp(lock, BAD_CAST "") == 0) {
			/* datastore is NOT locked */
			*error = nc_err_new(NC_ERR_OP_FAILED);
			nc_err_set(*error, NC_ERR_PARAM_MSG, "Target datastore is not locked.");
			retval = EXIT_FAILURE;
		} else {
			/* datastore is locked */
			if (xmlStrcmp(lock, BAD_CAST (session->session_id)) == 0) {
				/* the datastore is locked by request originating session */
				xmlSetProp (target_ds, BAD_CAST "lock", BAD_CAST "");
			} else {
				/* the datastore is locked by somebody else */
				*error = nc_err_new(NC_ERR_OP_FAILED);
				nc_err_set(*error, NC_ERR_PARAM_MSG, "Target datastore is locked by another session.");
				retval = EXIT_FAILURE;
			}
		}
		xmlFree(lock);
	}

	if (retval == EXIT_SUCCESS) {
		ncds_file_sync(file_ds);
	}

	return (retval);
}

char* ncds_file_getconfig (struct ncds_ds* ds, struct nc_session* session, NC_DATASTORE source, const struct nc_filter *filter, struct nc_err** error)
{
	struct ncds_ds_file* file_ds = (struct ncds_ds_file*)ds;
	xmlNodePtr target_ds, aux_node;
	xmlBufferPtr resultbuffer;
	char* data = NULL;

	/* check validity of function parameters */
	switch(source) {
	case NC_DATASTORE_RUNNING:
		target_ds = file_ds->running;
		break;
	case NC_DATASTORE_STARTUP:
		target_ds = file_ds->startup;
		break;
	case NC_DATASTORE_CANDIDATE:
		target_ds = file_ds->candidate;
		break;
	default:
		ERROR("%s: invalid target.", __func__);
		*error = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
		return (NULL);
		break;
	}

	resultbuffer = xmlBufferCreate();
	if (resultbuffer == NULL) {
		ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
		*error = nc_err_new(NC_ERR_OP_FAILED);
		return (NULL);
	}
	for (aux_node = target_ds->children; aux_node != NULL; aux_node = aux_node->next) {
		xmlNodeDump(resultbuffer, file_ds->xml, aux_node, 2, 1);
	}
	data = strdup((char *) xmlBufferContent(resultbuffer));
	xmlBufferFree(resultbuffer);

	return (data);
}
