/**
 * \file with_defaults.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of NETCONF's with-defaults capability defined in RFC 6243
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

#include <stdlib.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "datastore/edit_config.h"
#include "with_defaults.h"
#include "netconf_internal.h"

static NCDFLT_MODE ncdflt_basic_mode = NCDFLT_MODE_EXPLICIT;
static NCDFLT_MODE ncdflt_supported = (NCDFLT_MODE_ALL
		| NCDFLT_MODE_ALL_TAGGED
		| NCDFLT_MODE_TRIM
		| NCDFLT_MODE_EXPLICIT);

NCDFLT_MODE ncdflt_get_basic_mode()
{
	return (ncdflt_basic_mode);
}

void ncdflt_set_basic_mode(NCDFLT_MODE mode)
{
	/* if one of valid values, change the value */
	if (mode == NCDFLT_MODE_ALL
			|| mode == NCDFLT_MODE_TRIM
			|| mode == NCDFLT_MODE_EXPLICIT) {
		/* set basic mode */
		ncdflt_basic_mode = mode;

		/* if current basic mode is not in supported set, add it */
		if ((ncdflt_supported & ncdflt_basic_mode) == 0) {
			ncdflt_supported |= ncdflt_basic_mode;
		}
	}
}

void ncdflt_set_supported(NCDFLT_MODE modes)
{
	ncdflt_supported = ncdflt_basic_mode;
	ncdflt_supported |= (modes & NCDFLT_MODE_ALL) ? NCDFLT_MODE_ALL : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_ALL_TAGGED) ? NCDFLT_MODE_ALL_TAGGED : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_TRIM) ? NCDFLT_MODE_TRIM : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_EXPLICIT) ? NCDFLT_MODE_EXPLICIT : 0;
}

NCDFLT_MODE ncdflt_get_supported()
{
	return (ncdflt_supported);
}

int ncdflt_rpc_withdefaults(nc_rpc* rpc, NCDFLT_MODE mode)
{
	xmlNodePtr root, n;
	char* mode_s;

	if (rpc == NULL) {
		return (EXIT_FAILURE);
	}
	switch (mode) {
	case NCDFLT_MODE_ALL:
		mode_s = "report-all";
		break;
	case NCDFLT_MODE_ALL_TAGGED:
		mode_s = "report-all-tagged";
		break;
	case NCDFLT_MODE_TRIM:
		mode_s = "trim";
		break;
	case NCDFLT_MODE_EXPLICIT:
		mode_s = "explicit";
		break;
	default:
		ERROR("Invalid with-defaults mode.");
		return (EXIT_FAILURE);
		break;
	}

	/* all checks passed - save the flag */
	rpc->with_defaults = mode;

	/* modify XML */
	switch (nc_rpc_get_op(rpc)) {
	case NC_OP_COPYCONFIG:
	case NC_OP_GETCONFIG:
	case NC_OP_GET:
		/* ok, add the element */
		root = xmlDocGetRootElement(rpc->doc);
		if (root != NULL && root->children != NULL) {
			n = xmlNewChild(root->children, NULL, BAD_CAST "with-defaults", BAD_CAST mode_s);
			xmlNewNs(n, BAD_CAST "urn:ietf:params:xml:ns:yang:ietf-netconf-with-defaults", NULL);
		} else {
			ERROR("%s: Invalid RPC format.", __func__);
			return (EXIT_FAILURE);
		}
		break;
	default:
		/* no other operation allows using <with-defaults> */
		ERROR("Given RPC request can not contain <with-defaults> parameter.");
		return (EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
}

NCDFLT_MODE ncdflt_rpc_get_withdefaults(const nc_rpc* rpc)
{
	return (rpc->with_defaults);
}

static xmlNodePtr* fill_default(xmlDocPtr config, xmlNodePtr node, NCDFLT_MODE mode)
{
	xmlNodePtr *parents = NULL, *retvals = NULL;
	xmlNodePtr aux = NULL;
	xmlNsPtr ns;
	xmlChar* value, *name, *value2;
	int i, j, k, size = 0;

	if (mode == NCDFLT_MODE_DISABLED || mode == NCDFLT_MODE_EXPLICIT) {
		return (NULL);
	}

	/* do recursion */
	if (node->parent == NULL) {
		return (NULL);
	} else if (xmlStrcmp(node->parent->name, BAD_CAST "module") != 0) {
		/* we will get parent of the config's equivalent of the node */
		parents = fill_default(config, node->parent, mode);
	} else {
		/* we are in the root */
		aux = xmlDocGetRootElement(config);
		switch (mode) {
		case NCDFLT_MODE_ALL:
		case NCDFLT_MODE_ALL_TAGGED:
			/* return root element, create it if it does not exist */
			retvals = (xmlNodePtr*) malloc(2 * sizeof(xmlNodePtr));
			retvals[1] = NULL;
			if (aux == NULL) {
				/* create root element */
				name = xmlGetProp(node, BAD_CAST "name");
				aux = xmlNewDocNode(config, NULL, name, NULL);
				xmlDocSetRootElement(config, aux);
				xmlFree(name);
			}
			retvals[0] = aux;
			return (retvals);
			break;
		case NCDFLT_MODE_TRIM:
			/* return root element, do not create it if it does not exist */
			if (aux != NULL) {
				retvals = (xmlNodePtr*) malloc(2 * sizeof(xmlNodePtr));
				retvals[0] = aux;
				retvals[1] = NULL;
			}
			return (retvals);
			break;
		default:
			/* remove compiler warnings, but do nothing */
			break;
		}
	}
	if (parents == NULL) {
		return (NULL);
	}

	for (i = 0, j = 0; parents[i] != NULL; i++) {
		if (xmlStrcmp(node->name, BAD_CAST "default") == 0) {
			switch (mode) {
			case NCDFLT_MODE_ALL:
			case NCDFLT_MODE_ALL_TAGGED:
				/* we are at the end - set default content if needed */
				value = xmlGetProp(node, BAD_CAST "value");
				if (parents[i]->children == NULL) {
					/* element is empty -> fill it with the default value */
					xmlNodeSetContent(parents[i], value);
				} /* else do nothing, configuration data contain (non-)default value */

				if (mode == NCDFLT_MODE_ALL_TAGGED) {
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
			case NCDFLT_MODE_TRIM:
				/* we are at the end - remove element if it contain default value */
				if (parents[i]->children != NULL) {
					value = xmlGetProp(node, BAD_CAST "value");
					value2 = xmlNodeGetContent(parents[i]);
					if (xmlStrcmp(value, value2) == 0) {
						/* element contain default value, remove it */
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
						retvals = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
					}

					retvals[j] = aux;
					j++;
					retvals[j] = NULL; /* list terminating NULL */
				}
			}

			switch (mode) {
			case NCDFLT_MODE_ALL:
			case NCDFLT_MODE_ALL_TAGGED:
				if (k == j) {
					/* no new equivalent node found */
					if (size <= j + 1) {
						/* (re)allocate retvals list */
						size += 32;
						retvals = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
					}
					/* no new equivalent node found -> create one */
					retvals[j] = xmlNewChild(parents[i], NULL, name, NULL);
					j++;
					retvals[j] = NULL; /* list terminating NULL */
				}
				break;
			case NCDFLT_MODE_TRIM:
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

	return (retvals);
}

int ncdflt_default_values(xmlDocPtr config, const xmlDocPtr model, NCDFLT_MODE mode)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr defaults = NULL;
	int i;

	if (config == NULL || model == NULL) {
		return (EXIT_FAILURE);
	}

	if (mode == NCDFLT_MODE_DISABLED || mode == NCDFLT_MODE_EXPLICIT) {
		/* nothing to do */
		return (EXIT_SUCCESS);
	}

	/* create xpath evaluation context */
	if ((model_ctxt = xmlXPathNewContext(model)) == NULL) {
		WARN("%s: Creating XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (EXIT_FAILURE);
	}
	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST "yin", BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (EXIT_FAILURE);
	}
	defaults = xmlXPathEvalExpression(BAD_CAST "//yin:default", model_ctxt);
	if (defaults != NULL) {
		/* if report-all-tagged, add namespace for default attribute into the whole doc */
		if (mode == NCDFLT_MODE_ALL_TAGGED) {
			xmlNewNs(xmlDocGetRootElement(config), BAD_CAST "urn:ietf:params:xml:ns:netconf:default:1.0", BAD_CAST "wd");
		}
		/* process all defaults elements */
		for (i = 0; i < defaults->nodesetval->nodeNr; i++) {
			fill_default(config, defaults->nodesetval->nodeTab[i], mode);
		}

		xmlXPathFreeObject(defaults);
	}
	xmlXPathFreeContext(model_ctxt);

	return (EXIT_SUCCESS);
}

/**
 *
 */
static xmlNodePtr* remove_default_node(xmlDocPtr config, xmlNodePtr node)
{
	xmlNodePtr *parents, *retvals = NULL;
	xmlNodePtr aux = NULL;
	xmlChar* value, *name, *value2;
	int i, j, size = 0;

	/* do recursion */
	if (node->parent == NULL) {
		return (NULL);
	} else if (xmlStrcmp(node->parent->name, BAD_CAST "module") != 0) {
		/* we will get parent of the config's equivalent of the node */
		parents = remove_default_node(config, node->parent);
		if (parents == NULL) {
			return (NULL);
		}
	} else {
		/* we are in the root */
		aux = xmlDocGetRootElement(config);
		/* return root element, do not create it if it does not exist */
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
			for (aux = parents[i]->children; aux != NULL;
			                aux = aux->next) {
				if (xmlStrcmp(aux->name, name) == 0) {
					/* remember the node */
					if (size <= j + 1) {
						/* (re)allocate retvals list */
						size += 32;
						retvals = (xmlNodePtr*) realloc(retvals, size * sizeof(xmlNodePtr));
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
		WARN("%s: Creating XPath context failed.", __func__)
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
