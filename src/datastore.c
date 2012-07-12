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
#include "datastore.h"

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
};

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
	 * @brief Path to file containing configuration data, single file is
	 * used for all datastore types (running, startup, candidate).
	 */
	char* path;
	/**
	 * @brief File descriptor of opened file containing configuration data
	 */
	int fd;
	/**
	 * libxml2's document structure of the datastore
	 */
	xmlDocPtr xml;
	/**
	 * libxml2 Node pointers providing access to individual datastores 
	 */
	xmlNodePtr candidate, running, startup;
};

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
static struct ncds_ds_list *datastores_get_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;

	for (ds_iter = datastores; ds_iter != NULL; ds_iter = ds_iter->next) {
		if (ds_iter->datastore != NULL && ds_iter->datastore->id == id) {
			break;
		}
	}

	return (ds_iter);
}

static struct ncds_ds_list *datastores_detach_ds(ncds_id id)
{
	struct ncds_ds_list *ds_iter;
	struct ncds_ds_list *ds_prev = NULL;

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
		ds_iter->next = NULL;
	}

	return (ds_iter);
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

void ncds_free(struct ncds_ds* datastore)
{
	struct ncds_ds_list *ds = NULL;
	struct ncds_ds *aux;

	if (datastore == NULL) {
		WARN ("%s: no datastore to free.", __func__);
		return;
	}

	if (datastore->id > 0) {
		/* datastore is initialized and must be in the datastores list */
		ds = datastores_detach_ds(datastore->id);
		aux = ds->datastore;
	} else {
		/* datastore to free is uninitialized and will be only freed */
		aux = datastore;
	}

	/* close and free the datastore itself */
	if (aux != NULL) {
		switch (aux->type) {
		case NCDS_TYPE_FILE:
//			ncds_file_free((struct ncds_ds_file*) (aux));
			break;
		default:
			ERROR("Unsupported datastore implementation to be freed.");
			break;
		}
		free(ds);
	}

	/* free the datastore list structure */
	if (ds != NULL) {
		free(ds);
	}
}

#if 0
/** DOXYGEN COMENT FROM .h FILE
 * @ingroup store
 * @brief File datastore implementation type setter for the path parameter.
 * @param[in] datastore Datastore structure to be configured.
 * @param[in] path File path to the file storing configuration datastores.
 * @return 0 on success, nonzero on error.
 *	   -1 Invalid datastore
 *	   -2 Invalid path to a file
 */
#endif
/**
 * @brief Assigns path to datastore to file datastore structure
 *
 * Checks if the file exist and is accessible for reading and writing.
 * If the file does not exist it is created. File is opened and file
 * descriptor is stored in the structure
 *
 * @param datastore Structure to assign path to.
 * @param[in] path Path to datastore file.
 *
 * return 0 on success
 * 	  -1 Invalid datastore
 *	  -2 Invalid path ((does not exist && can not be created) || insufficient rights)
 */
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
	if (access (path, F_OK)) {
		WARN ("File %s does not exist.", path);
		/* try to create it */
		file_ds->fd = open (path, O_CREAT|O_RDWR);
		if (file_ds->fd == -1) {
			ERROR ("File %s can not be created.", path);
			return -2;
		} else {
			WARN ("File %s was created.", path);
		}
	} else if (access (path, W_OK|R_OK)) {
		ERROR ("Insufficient rights for manipulation with file %s.", path);
		return -2;
	} else {
		file_ds->fd = open (path, O_RDWR);
		if (file_ds->fd == -1) {
			ERROR ("File %s can not be opened in RW mode. (Possibly EUID != UID)", path);
			return -2;
		}
	}
	file_ds->path = strdup(path);

	return 0;
}

/**
 * @brief Generate unique pseudorandom id
 *
 * return unique datastorage id
 */
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

/**
 * @brief Checks if structure of XML is matches the expected one
 *
 * @param[in] doc Document to check.
 *
 * @return non-zero if matches zero if not.
 */
int ds_structure_check (xmlDocPtr doc)
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
			WARN ("Ignoring unknown element %s.", ds->name);
		}
	}

	if (candidate && running && startup) {
		return 1;
	}

	return 0;
}

/**
 * @brief Create datastore internal structure
 *
 * @param[out] candidate Pointer to candidate part of datastore
 * @param[out] running Pointer to running part of datastore
 * @param[out] startup Pointer to startup part of datastore
 * @param[out] mem Pointer to serialized xml, if mem and len not NULL
 * @param[out] len length of serialized xml, if mem and len not NULL
 *
 * return xml document holding basic structure
 */
xmlDocPtr ds_structure_create (xmlNodePtr *candidate, xmlNodePtr *running, xmlNodePtr *startup, char ** mem, int *len)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNsPtr ns;

	if ((doc = xmlNewDoc (BAD_CAST "1.0")) == NULL) {
		goto doc_failed;
	}

	if ((root = xmlNewDocNode (doc, NULL, BAD_CAST "datastores", NULL)) == NULL) {
		goto root_failed;
	}
	xmlDocSetRootElement (doc, root);

	if ((ns = xmlNewNs (root, BAD_CAST "urn:cesnet:tmc:datastores", BAD_CAST "datastores")) == NULL) {
		goto ns_failed;
	}

	if ((*candidate = xmlNewChild (root, NULL, BAD_CAST "candidate", NULL)) == NULL) {
		goto candidate_failed;
	}
	xmlNewNsProp (*candidate, ns, BAD_CAST "lock", BAD_CAST "none");

	if ((*running = xmlNewChild (root, NULL, BAD_CAST "running", NULL)) == NULL) {
		goto running_failed;
	}
	xmlNewNsProp (*running, ns, BAD_CAST "lock", BAD_CAST "none");

	if ((*startup = xmlNewChild (root, NULL, BAD_CAST "startup", NULL)) == NULL) {
		goto startup_failed;
	}
	xmlNewNsProp (*startup, ns, BAD_CAST "lock", BAD_CAST "none");

	if (mem != NULL && len != NULL) {
		xmlDocDumpFormatMemory (doc, (xmlChar**)mem, len, 1);
	}

	return doc;

/* big nice cleanup */
startup_failed:
	xmlFreeNode (*running);
running_failed:
	xmlFreeNode (*candidate);
candidate_failed:
	xmlFreeNs (ns);
ns_failed:
	xmlFreeNode (root);
root_failed:
	xmlFreeDoc (doc);
doc_failed:
	return NULL;
}

/**
 * @ingroup store
 * @brief Initialization of file datastore
 *
 * @file_ds File datastore structure 
 *
 * @return 0 on success, non-zero else
 */
int ncds_file_init (struct ncds_ds_file* file_ds)
{
	struct stat st;
	char * new_path, *mem;
	int len;

	file_ds->xml = xmlReadFd (file_ds->fd, NULL, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN);
	if (file_ds->xml == NULL || ds_structure_check (file_ds->xml) == 0) {
		WARN ("Failed to parse XML in file.");
		/* Unable to determine size or size bigger than 0 */
		if (fstat(file_ds->fd, &st) || st.st_size > 0) {
			WARN ("File %s contains some data.", file_ds->path);
			close (file_ds->fd);
			xmlFreeDoc (file_ds->xml);
			/* Create file based on original name */
			asprintf (&new_path, "%s.XXXXXX", file_ds->path);
			file_ds->fd = mkstemp (new_path);
			if (file_ds->fd == -1) {
				ERROR ("Can not create alternative file.");
				free (new_path);
				return EXIT_FAILURE;
			}
			free (file_ds->path);
			file_ds->path = new_path;
			WARN("Using file %s to prevent data loss.", file_ds->path);
		}
		file_ds->xml = ds_structure_create (&file_ds->candidate, &file_ds->running, &file_ds->startup, &mem, &len);
		if (file_ds->xml == NULL) {
			return EXIT_FAILURE;
		}
		write (file_ds->fd, mem, len);
		WARN ("File %s was empty. Basic structure created.", file_ds->path);
	}

	return EXIT_SUCCESS;
}

ncds_id ncds_init (struct ncds_ds* datastore)
{
	struct ncds_ds_list * item;

	if (datastore == NULL) {
		return -1;
	}

	/** \todo data model validation */

	switch(datastore->type) {
	case NCDS_TYPE_FILE:
		if (ncds_file_init ((struct ncds_ds_file*)datastore)) {
			ERROR ("File-specific datastore initialization failed.");
			return -2;
		}
		break;
	default:
		ERROR("Unsupported datastore implementation required.");
		return -3;
	}
	
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
