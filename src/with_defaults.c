/**
 * \file with_defaults.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of NETCONF's with-defaults capability defined in RFC 6243
 *
 * Copyright (c) 2012-2014 CESNET, z.s.p.o.
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

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "datastore/edit_config.h"
#include "with_defaults.h"
#include "netconf_internal.h"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

/*
 * From internal.c to be used by ncdflt_set_*() functions to detect if the
 * with-default capability is enabled
 */
extern int nc_init_flags;

static NCWD_MODE ncdflt_basic_mode = NCWD_MODE_NOTSET;
static NCWD_MODE ncdflt_supported = NCWD_MODE_NOTSET;

NCWD_MODE ncdflt_get_basic_mode()
{
	return (ncdflt_basic_mode);
}

void ncdflt_set_basic_mode(NCWD_MODE mode)
{
	if (nc_init_flags & NC_INIT_WD) {
		/* if one of valid values, change the value */
		if (mode == NCWD_MODE_ALL
				|| mode == NCWD_MODE_TRIM
				|| mode == NCWD_MODE_EXPLICIT) {
			/* set basic mode */
			ncdflt_basic_mode = mode;

			/* if current basic mode is not in the supported set, add it */
			if ((ncdflt_supported & ncdflt_basic_mode) == 0) {
				ncdflt_supported |= ncdflt_basic_mode;
			}
		}
	}
}

void ncdflt_set_supported(NCWD_MODE modes)
{
	if (nc_init_flags & NC_INIT_WD) {
		ncdflt_supported = ncdflt_basic_mode;
		ncdflt_supported |= (modes & NCWD_MODE_ALL) ? NCWD_MODE_ALL : 0;
		ncdflt_supported |= (modes & NCWD_MODE_ALL_TAGGED) ? NCWD_MODE_ALL_TAGGED : 0;
		ncdflt_supported |= (modes & NCWD_MODE_TRIM) ? NCWD_MODE_TRIM : 0;
		ncdflt_supported |= (modes & NCWD_MODE_EXPLICIT) ? NCWD_MODE_EXPLICIT : 0;
	}
}

NCWD_MODE ncdflt_get_supported()
{
	return (ncdflt_supported);
}

NCWD_MODE ncdflt_rpc_get_withdefaults(const nc_rpc* rpc)
{
	return (rpc->with_defaults);
}

static xmlNodePtr* fill_default(xmlDocPtr config, xmlNodePtr node, const char* namespace, NCWD_MODE mode, xmlNodePtr** created)
{
	xmlNodePtr *parents = NULL, *retvals = NULL, *created_local = NULL, *aux_nodeptr;
	xmlNodePtr aux = NULL;
	xmlNsPtr ns;
	xmlChar* value = NULL, *name, *value2;
	int i, j, k, size = 0;
	static int created_count = 0;
	static int created_size = 0;

	if (mode == NCWD_MODE_NOTSET || mode == NCWD_MODE_EXPLICIT) {
		return (NULL);
	}

	if (created == NULL) {
		/* initial (not recursive) call */
		created_count = 0;
		created_size = 32;
		created_local = malloc(created_size * sizeof(xmlNodePtr));
		created_local[created_count] = NULL; /* list terminating byte */
	} else {
		created_local = *created;
	}

	/* do recursion */
	if (node->parent == NULL) {
		if (created == NULL) {
			if (retvals == NULL) {
				for(i = created_count-1; i >= 0; i--) {
					if (created_local[i]->children == NULL) {
						/* created parent element, but default value was not finally
						 * created and no other children element exists -> remove
						 * the created element
						 */
						xmlUnlinkNode(created_local[i]);
						xmlFreeNode(created_local[i]);
					}
				}
			}
			/* free in last recursion call */
			created_count = 0;
			free(created_local);
		}
		return (NULL);
	} else if (xmlStrcmp(node->parent->name, BAD_CAST "module") != 0) {
		/* we will get parent of the config's equivalent of the node */
		parents = fill_default(config, node->parent, namespace, mode, &created_local);

		/* if we are in augment node, just go through */
		if ((xmlStrcmp(node->name, BAD_CAST "augment") == 0) &&
				(xmlStrcmp(node->ns->href, BAD_CAST NC_NS_YIN) == 0)) {
			return (parents);
		}
	} else {
		/* we are in the root */
		aux = config->children;
		switch (mode) {
		case NCWD_MODE_ALL:
		case NCWD_MODE_ALL_TAGGED:
			/* return root element, create it if it does not exist */
			retvals = (xmlNodePtr*) malloc(2 * sizeof(xmlNodePtr));
			if (retvals == NULL) {
				ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
				created_count = 0;
				free(created_local);
				if (created) {
					*created = NULL;
				}
				return (NULL);
			}
			retvals[1] = NULL;
			/* create root element */
			name = xmlGetProp(node, BAD_CAST "name");
			/* search in all the root elements */
			for (; aux != NULL; aux = aux->next) {
				if (xmlStrcmp(aux->name, name) == 0) {
					break;
				}
			}
			if (aux == NULL) {
				aux = xmlNewDocNode(config, NULL, name, NULL);
				if (config->children == NULL) {
					xmlDocSetRootElement(config, aux);
				} else {
					xmlAddSibling(config->children, aux);
				}
				/* set namespace */
				ns = xmlNewNs(aux, BAD_CAST namespace, NULL);
				xmlSetNs(aux, ns);

				/* remember created node, for later remove if no default child will be created */
				if (created_count == created_size-1) {
					/* (re)allocate created list */
					created_size += 32;
					aux_nodeptr = realloc(created_local, created_size * sizeof(xmlNodePtr));
					if (aux_nodeptr == NULL) {
						ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
						/* we're in real troubles here */
						created_count = 0;
						free(created_local);
						if (created) {
							*created = NULL;
						}
						return (NULL);
					}
					created_local = aux_nodeptr;
					if (created) {
						*created = aux_nodeptr;
					}
				}
				created_local[created_count++] = aux;
				created_local[created_count] = NULL; /* list terminating byte */
			}
			xmlFree(name);

			/* if report-all-tagged, add namespace for default attribute into the whole doc */
			if (mode == NCWD_MODE_ALL_TAGGED) {
				xmlNewNs(aux, BAD_CAST "urn:ietf:params:xml:ns:netconf:default:1.0", BAD_CAST "wd");
			}

			retvals[0] = aux;
			return (retvals);
			break;
		case NCWD_MODE_TRIM:
			/* search in all the root elements */
			name = xmlGetProp(node, BAD_CAST "name");
			for (; aux != NULL; aux = aux->next) {
				if (xmlStrcmp(aux->name, name) == 0) {
					break;
				}
			}
			/* return root element, do not create it if it does not exist */
			if (aux != NULL) {
				retvals = (xmlNodePtr*) malloc(2 * sizeof(xmlNodePtr));
				if (retvals == NULL) {
					ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
					xmlFree(name);
					created_count = 0;
					free(created_local);
					if (created) {
						*created = NULL;
					}
					return (NULL);
				}
				retvals[0] = aux;
				retvals[1] = NULL;
			}
			xmlFree(name);
			return (retvals);
			break;
		default:
			/* remove compiler warnings, but do nothing */
			break;
		}
	}
	if (parents == NULL) {
		if (created == NULL) {
			if (retvals == NULL) {
				for(i = created_count-1; i >= 0; i--) {
					if (created_local[i]->children == NULL) {
						/* created parent element, but default value was not finally
						 * created and no other children element exists -> remove
						 * the created element
						 */
						xmlUnlinkNode(created_local[i]);
						xmlFreeNode(created_local[i]);
					}
				}
			}
			/* free in last recursion call */
			free(created_local);
			created_count = 0;
		}
		return (NULL);
	}

	for (i = 0, j = 0; parents[i] != NULL; i++) {
		if (xmlStrcmp(node->name, BAD_CAST "default") == 0) {
			switch (mode) {
			case NCWD_MODE_ALL:
			case NCWD_MODE_ALL_TAGGED:
				/* we are at the end - set default content if needed */
				value = xmlGetProp(node, BAD_CAST "value");
				if (parents[i]->children == NULL) {
					/* element is empty -> fill it with the default value */
					xmlNodeSetContent(parents[i], value);
				} /* else do nothing, configuration data contain (non-)default value */

				if (mode == NCWD_MODE_ALL_TAGGED) {
					value2 = xmlNodeGetContent(parents[i]);
					if (xmlStrcmp(value, value2) == 0) {
						/* add default attribute if element has default value */
						aux = xmlDocGetRootElement(config);
						for (ns = aux->nsDef; ns != NULL; ns = ns->next) {
							if (xmlStrcmp(ns->href, BAD_CAST "urn:ietf:params:xml:ns:netconf:default:1.0") == 0) {
								break;
							}
						}
						xmlNewNsProp(parents[i], ns, BAD_CAST "default", BAD_CAST "true");
					}
					xmlFree(value2);
				}

				xmlFree(value);
				/* continue to another parent node in the list to process */
				continue;
			case NCWD_MODE_TRIM:
				/* we are at the end - remove element if it contains default value */
				if (parents[i]->children != NULL) {
					value = xmlGetProp(node, BAD_CAST "value");
					value2 = xmlNodeGetContent(parents[i]);
					if (xmlStrcmp(value, value2) == 0) {
						/* element contains default value, remove it */
						xmlUnlinkNode(parents[i]);
						xmlFreeNode(parents[i]);
					}
					xmlFree(value);
					xmlFree(value2);
				}
				break;
			default:
				/* remove compiler warnings, but do nothing */
				break;
			}
		} else {
			/* remember the number of retvals */
			k = j;
			/* find node's equivalents in config */
			name = xmlGetProp(node, BAD_CAST "name");
			for(aux = parents[i]->children; aux != NULL; aux = aux->next) {
				if (xmlStrcmp(aux->name, name) == 0) {
					/* remember the node */
					if (size <= j+1) {
						/* (re)allocate retvals list */
						size += 32;
						aux_nodeptr = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
						if (aux_nodeptr == NULL) {
							ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
							/* we're in real troubles here */
							free(retvals);
							return (NULL);
						}
						retvals = aux_nodeptr;
					}

					retvals[j] = aux;
					j++;
					retvals[j] = NULL; /* list terminating NULL */
				}
			}

			switch (mode) {
			case NCWD_MODE_ALL:
			case NCWD_MODE_ALL_TAGGED:
				if (k == j && xmlStrcmp(node->name, BAD_CAST "list") != 0) {
					/* check, that new node is not a "presence" element */
					for (aux = node->children; aux != NULL; aux = aux->next) {
						if (xmlStrcmp(aux->name, BAD_CAST "presence") == 0) {
							break;
						}
					}
					if (aux != NULL) {
						/* presence definition found -> leave the case */
						break;
					}
					/* no new equivalent node found -> create one, but only if it is not supposed to be list */
					if (size <= j + 1) {
						/* (re)allocate retvals list */
						size += 32;
						aux_nodeptr = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
						if (aux_nodeptr == NULL) {
							ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
							/* we're in real troubles here */
							free(retvals);
							return (NULL);
						}
						retvals = aux_nodeptr;
					}
					/* no new equivalent node found -> create one */
					retvals[j] = xmlNewChild(parents[i], parents[i]->ns, name, NULL);
					j++;
					retvals[j] = NULL; /* list terminating NULL */

					/* remember created node, for later remove if no default child will be created */
					if (created_count == created_size-1) {
						/* (re)allocate created list */
						created_size += 32;
						aux_nodeptr = realloc(created_local, created_size * sizeof(xmlNodePtr));
						if (aux_nodeptr == NULL) {
							ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
							/* we're in real troubles here */
							created_count = 0;
							free(created_local);
							if (created) {
								*created = NULL;
							}
							return (NULL);
						}
						created_local = aux_nodeptr;
					}
					created_local[created_count++] = retvals[j-1];
					created_local[created_count] = NULL; /* list terminating byte */
				}
				break;
			case NCWD_MODE_TRIM:
				/* nothing needed */
				break;
			default:
				/* remove compiler warnings, but do nothing */
				break;
			}
			xmlFree(name);
		}
	}
	if (parents != NULL) {
		free(parents);
	}
	if (created == NULL) {
		if (retvals == NULL) {
			for(i = created_count-1; i >= 0; i--) {
				if (created_local[i]->children == NULL) {
					/* created parent element, but default value was not finally
					 * created and no other children element exists -> remove
					 * the created element
					 */
					xmlUnlinkNode(created_local[i]);
					xmlFreeNode(created_local[i]);
				}
			}
		}
		/* free in last recursion call */
		free(created_local);
		created_count = 0;
	}

	return (retvals);
}

int ncdflt_default_values(xmlDocPtr config, const xmlDocPtr model, NCWD_MODE mode)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr defaults = NULL, query;
	xmlNodePtr root;
	xmlChar* namespace = NULL;
	int i;

	if (config == NULL || model == NULL) {
		return (EXIT_FAILURE);
	}

	if (mode == NCWD_MODE_NOTSET || mode == NCWD_MODE_EXPLICIT) {
		/* nothing to do */
		return (EXIT_SUCCESS);
	}

	/* create xpath evaluation context */
	if ((model_ctxt = xmlXPathNewContext(model)) == NULL) {
		WARN("%s: Creating the XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST "yin", BAD_CAST NC_NS_YIN) != 0) {
		ERROR("%s: Registering yin namespace for the model xpath context failed.", __func__);
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}
	if ((query = xmlXPathEvalExpression(BAD_CAST "/yin:module/yin:namespace", model_ctxt)) == NULL) {
		ERROR("%s: Unable to get namespace from the data model.", __func__);
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}
	if (xmlXPathNodeSetIsEmpty(query->nodesetval) || (namespace = xmlGetProp(query->nodesetval->nodeTab[0], BAD_CAST "uri")) == NULL) {
		ERROR("%s: Unable to get namespace from the data model.", __func__);
		xmlFree(namespace);
		xmlXPathFreeObject(query);
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}
	xmlXPathFreeObject(query);

	if ((defaults = xmlXPathEvalExpression(BAD_CAST "/yin:module/yin:container//yin:default", model_ctxt)) != NULL) {
		if (!xmlXPathNodeSetIsEmpty(defaults->nodesetval)) {
			/* if report-all-tagged, add namespace for default attribute into the whole doc */
			root = xmlDocGetRootElement(config);
			if (mode == NCWD_MODE_ALL_TAGGED && root != NULL) {
				xmlNewNs(root, BAD_CAST "urn:ietf:params:xml:ns:netconf:default:1.0", BAD_CAST "wd");
			}
			/* process all defaults elements */
			for (i = 0; i < defaults->nodesetval->nodeNr; i++) {
				fill_default(config, defaults->nodesetval->nodeTab[i], (char*)namespace, mode, NULL);
			}
		}
		xmlXPathFreeObject(defaults);
	}
	xmlFree(namespace);
	xmlXPathFreeContext(model_ctxt);

	return (EXIT_SUCCESS);
}

/**
 *
 */
static xmlNodePtr* remove_default_node(xmlDocPtr config, xmlNodePtr node)
{
	xmlNodePtr *parents, *retvals = NULL, *aux_nodeptr;
	xmlNodePtr aux = NULL;
	xmlChar* value, *name, *value2;
	int i, j, size = 0;

	/* do recursion */
	if (node->parent == NULL) {
		return (NULL);
	} else if (xmlStrcmp(node->parent->name, BAD_CAST "module") != 0) {
		/* we will get the parent of the config's equivalent of the node */
		parents = remove_default_node(config, node->parent);
		if (parents == NULL) {
			return (NULL);
		}
	} else {
		/* we are in the root */
		aux = xmlDocGetRootElement(config);
		/* return the root element, do not create it if it does not exist */
		if (aux != NULL) {
			retvals = (xmlNodePtr*) malloc(2 * sizeof(xmlNodePtr));
			retvals[0] = aux;
			retvals[1] = NULL;
		}
		return (retvals);
	}

	for (i = 0, j = 0; parents[i] != NULL; i++) {
		if (xmlStrcmp(node->name, BAD_CAST "default") == 0) {
			/* we are at the end */
			/* check if element has specified default attribute */
			value2 = xmlGetNsProp(parents[i], BAD_CAST "default", BAD_CAST "urn:ietf:params:xml:ns:netconf:default:1.0");
			if ((xmlStrcmp(value2, BAD_CAST "true") == 0)
					|| (xmlStrcmp(value2, BAD_CAST "1") == 0)) {
				/* check if it has the real default value - stupid RFC requires all these rules :( */
				value = xmlGetProp(node, BAD_CAST "value");
				if (xmlStrcmp(value, value2) != 0) {
					/* error should be generated by the server */
					xmlFree(value);
					xmlFree(value2);
					return (NULL);
				} else {
					/* element contain default value, remove it */
					xmlUnlinkNode(parents[i]);
					xmlFreeNode(parents[i]);
				}
				xmlFree(value);
			} /* else ignore this element and go to another */
			xmlFree(value2);
		} else {
			/* find node's equivalents in config */
			name = xmlGetProp(node, BAD_CAST "name");
			for (aux = parents[i]->children; aux != NULL; aux = aux->next) {
				if (xmlStrcmp(aux->name, name) == 0) {
					/* remember the node */
					if (size <= j + 1) {
						/* (re)allocate retvals list */
						size += 32;
						aux_nodeptr = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
						if (aux_nodeptr == NULL) {
							ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
							/* we're in real troubles here */
							free(retvals);
							return (NULL);
						}
						retvals = aux_nodeptr;
					}

					retvals[j] = aux;
					j++;
					retvals[j] = NULL; /* list terminating NULL */
				}
			}
			xmlFree(name);
		}
	}
	if (parents != NULL) {
		free(parents);
	}

	if (xmlStrcmp(node->name, BAD_CAST "default") == 0) {
		/* success and end of recursion */
		retvals = (xmlNodePtr*) malloc(sizeof(xmlNodePtr));
		retvals[0] = NULL;
	}
	return (retvals);
}

int ncdflt_default_clear(xmlDocPtr config, const xmlDocPtr model)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr defaults = NULL;
	xmlNodePtr* r;
	int i, retval = EXIT_SUCCESS;

	if (config == NULL || model == NULL) {
		return (EXIT_FAILURE);
	}

	/* create xpath evaluation context */
	if ((model_ctxt = xmlXPathNewContext(model)) == NULL) {
		WARN("%s: Creating the XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST "yin", BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}
	defaults = xmlXPathEvalExpression(BAD_CAST "//yin:default", model_ctxt);
	if (defaults != NULL) {
		/* process all defaults elements */
		for (i = 0; i < defaults->nodesetval->nodeNr; i++) {
			if ((r = remove_default_node(config, defaults->nodesetval->nodeTab[i])) == NULL) {
				retval = EXIT_FAILURE;
				break;
			} else {
				free(r);
			}
		}

		xmlXPathFreeObject(defaults);
	}
	xmlXPathFreeContext(model_ctxt);

	return (retval);
}
