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

#include <libxml/tree.h>

#include "../../netconf_internal.h"
#include "../datastore_internal.h"
#include "datastore_file.h"


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
 * @brief Checks if structure of XML is matches the expected one
 *
 * @param[in] doc Document to check.
 *
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
static xmlDocPtr file_structure_create (xmlNodePtr *candidate, xmlNodePtr *running, xmlNodePtr *startup, char ** mem, int *len)
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
	if (file_ds->xml == NULL || file_structure_check (file_ds->xml) == 0) {
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
		file_ds->xml = file_structure_create (&file_ds->candidate, &file_ds->running, &file_ds->startup, &mem, &len);
		if (file_ds->xml == NULL) {
			return EXIT_FAILURE;
		}
		write (file_ds->fd, mem, len);
		WARN ("File %s was empty. Basic structure created.", file_ds->path);
	}

	return EXIT_SUCCESS;
}

