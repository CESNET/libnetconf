/**
 * \file edit-config.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief NETCONF edit-config implementation independent on repository
 * implementation.
 *
 * Copyright (C) 2011-2012 CESNET, z.s.p.o.
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
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "edit_config.h"
#include "../netconf.h"
#include "../netconf_internal.h"

#define XPATH_BUFFER 1024

#define NC_EDIT_OP_MERGE_STRING "merge"
#define NC_EDIT_OP_CREATE_STRING "create"
#define NC_EDIT_OP_DELETE_STRING "delete"
#define NC_EDIT_OP_REPLACE_STRING "replace"
#define NC_EDIT_OP_REMOVE_STRING "remove"
#define NC_EDIT_ATTR_OP "operation"

#define NC_NS_YIN "urn:ietf:params:xml:ns:yang:yin:1"
#define NC_NS_YIN_ID "yin"


typedef enum {
	NC_CHECK_EDIT_DELETE = NC_EDIT_OP_DELETE,
	NC_CHECK_EDIT_CREATE = NC_EDIT_OP_CREATE
} NC_CHECK_EDIT_OP;


int nc_nscmp(xmlNodePtr reference, xmlNodePtr node)
{
        int in_ns = 1;

        if (reference->ns != NULL) {
                /* if filter has got specified no namespace now the NETCONF base namespace must be skipped */
                if (!strcmp((char *) reference->ns->href, NC_NS_BASE10))
                        return 0;

                in_ns = 0;
                if (node->ns != NULL) {
                        if(!strcmp((char *) reference->ns->href, (char *) node->ns->href)) {
                                in_ns = 1;
                        }
                }
        }
        return (in_ns == 1 ? 0 : 1);
}

/**
 * \brief Get value of the operation attribute of the \<node\> element.
 * If no such attribute is present, defop parameter is used and returned.
 *
 * \param[in] node XML element to analyse
 * \param[in] defop Default operation to use if no specific operation is present
 * \param[out] err NETCONF error structure to store error description
 *
 * \return NC_OP_TYPE_ERROR on error, valid NC_OP_TYPE values otherwise
 */
static NC_EDIT_OP_TYPE get_operation(xmlNodePtr node, NC_EDIT_DEFOP_TYPE defop, struct nc_err** error)
{
	char *operation = NULL;
	NC_EDIT_OP_TYPE op;

	/* get specific operation the node */
	if ((operation = (char *) xmlGetNsProp(node, BAD_CAST NC_EDIT_ATTR_OP, BAD_CAST NC_NS_BASE)) != NULL) {
		if (!strcmp(operation, NC_EDIT_OP_MERGE_STRING)) {
			op = NC_EDIT_OP_MERGE;
		} else if (!strcmp(operation, NC_EDIT_OP_REPLACE_STRING)) {
			op = NC_EDIT_OP_REPLACE;
		} else if (!strcmp(operation, NC_EDIT_OP_CREATE_STRING)) {
			op = NC_EDIT_OP_CREATE;
		} else if (!strcmp(operation, NC_EDIT_OP_DELETE_STRING)) {
			op = NC_EDIT_OP_DELETE;
		} else if (!strcmp(operation, NC_EDIT_OP_REMOVE_STRING)) {
			op = NC_EDIT_OP_REMOVE;
		} else {
			if (error != NULL) {
				*error = nc_err_new (NC_ERR_BAD_ATTR);
				nc_err_set (*error, NC_ERR_PARAM_INFO_BADATTR, NC_EDIT_ATTR_OP);
			}
			op = NC_EDIT_OP_ERROR;
		}
	} else {
		op = (NC_EDIT_OP_TYPE) defop;
	}
   free (operation);

	return op;
}

/**
 * @brief Get all nodes that have default value defined in the model.
 * @param[in] model Configuration data model to search for nodes with defined
 * default values.
 * @return The list of found nodes with default value definitions.
 */
xmlXPathObjectPtr get_defaults_list(xmlDocPtr model)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr defaults = NULL;

	if (model == NULL) {
		return (NULL);
	}

	/* create xpath evaluation context */
	if ((model_ctxt = xmlXPathNewContext(model)) == NULL) {
		return (NULL);
	}

	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST "yin", BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (NULL);
	}

	defaults = xmlXPathEvalExpression(BAD_CAST "//yin:default", model_ctxt);
	if (defaults != NULL) {
		if (xmlXPathNodeSetIsEmpty(defaults->nodesetval)) {
			xmlXPathFreeObject(defaults);
			defaults = (NULL);
		}
	}
	xmlXPathFreeContext(model_ctxt);

	return (defaults);
}

/**
 * \brief Get all key elements from configuration data model
 *
 * \param model         XML form (YIN) of the configuration data model.
 *
 * \return              keyList with references to all keys in data model.
 */
keyList get_keynode_list(xmlDocPtr model)
{
	xmlXPathContextPtr model_ctxt = NULL;
	xmlXPathObjectPtr result = NULL;

	if (model == NULL) {
		return (NULL);
	}

	/* create xpath evaluation context */
	model_ctxt = xmlXPathNewContext(model);
	if (model_ctxt == NULL) {
		return (NULL);
	}

	if (xmlXPathRegisterNs(model_ctxt, BAD_CAST NC_NS_YIN_ID, BAD_CAST NC_NS_YIN) != 0) {
		xmlXPathFreeContext(model_ctxt);
		return (NULL);
	}

	result = xmlXPathEvalExpression(BAD_CAST "//" NC_NS_YIN_ID ":key", model_ctxt);
	if (result != NULL) {
		if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
			xmlXPathFreeObject(result);
			result = (NULL);
		}
	}
	xmlXPathFreeContext(model_ctxt);

	return (keyList) result;
}

/**
 * \brief Get all key nodes for the specific element.
 *
 * \param[in] keys List of key elements from configuration data model.
 * \param[in] node Node for which the key elements are needed.
 * \param[out] result List of pointers to the key elements from node's children.
 * \return Zero on success, non-zero otherwise.
 */
static int get_keys(keyList keys, xmlNodePtr node, xmlNodePtr **result)
{
	xmlChar *str = NULL;
	char* s, *token;
	int i, j, c;

	assert(keys != NULL);
	assert(node != NULL);
	assert(result != NULL);

	*result = NULL;

	for (j = 0; j < keys->nodesetval->nodeNr; j++) {
		/* get corresponding key definition from the data model */
		// name = xmlGetNsProp (keys->nodesetval->nodeTab[i]->parent, BAD_CAST "name", BAD_CAST NC_NS_YIN);
		if ((str = xmlGetProp(keys->nodesetval->nodeTab[j]->parent, BAD_CAST "name")) == NULL) {
			continue;
		}
		if (xmlStrcmp(str, node->name)) {
			xmlFree(str);
			continue;
		}
		xmlFree(str);

		/* now get the key nodes from the xml document */
		/* get the name of the key node(s) from the 'value' attribute in key element in data model */
		if ((str = xmlGetProp(keys->nodesetval->nodeTab[j], BAD_CAST "value")) == NULL) {
			continue;
		}

		/* attribute have the form of space-separated list of key nodes */
		/* get the number of keys */
		for (i = 0, c = 1; i < strlen((char*)str); i++) {
			if (str[i] == ' ') {
				c++;
			}
		}
		/* allocate sufficient array of pointers to key nodes */
		*result = (xmlNodePtr*)calloc(c + 1, sizeof(xmlNodePtr));
		if (*result == NULL) {
			xmlFree (str);
			return (EXIT_FAILURE);
		}

		/* and now process all key nodes defined in attribute value list */
		for (i = 0, s = (char*) str; i < c; i++, s = NULL) {
			token = strtok(s, " ");
			if (token == NULL) {
				break;
			}

			/* get key nodes in original xml tree - all keys are needed */
			(*result)[i] = node->children;
			while (((*result)[i] != NULL) && strcmp(token, (char*) ((*result)[i])->name)) {
				(*result)[i] = ((*result)[i])->next;
			}
			if ((*result)[i] == NULL) {
				xmlFree (str);
				free(*result);
				*result = NULL;
				return (EXIT_FAILURE);
			}
		}

		xmlFree (str);
	}

	return (EXIT_SUCCESS);
}

/**
 * \brief Decide if the given children is a key element of the parent.
 *
 * \param[in] parent Parent element which key node is checked.
 * \param[in] children Element to decide if it is a key element of the parent
 * \param[in] keys List of key elements from configuration data model.
 * \return Zero if the given children is NOT the key element of the parent.
 */
static int is_key(xmlNodePtr parent, xmlNodePtr children, keyList keys)
{
	xmlChar *str = NULL;
	char *s, *token;
	int i;

	assert(parent != NULL);
	assert(children != NULL);

	if (keys == NULL) {
		/* there are no keys */
		return 0;
	}

	for (i = 0; i < keys->nodesetval->nodeNr; i++) {
		/* get corresponding key definition from the data model */
		// name = xmlGetNsProp (keys->nodesetval->nodeTab[i]->parent, BAD_CAST "name", BAD_CAST NC_NS_YIN);
		if ((str = xmlGetProp(keys->nodesetval->nodeTab[i]->parent, BAD_CAST "name")) == NULL) {
			continue;
		}
		if (xmlStrcmp(str, parent->name)) {
			xmlFree(str);
			continue;
		}
		xmlFree(str);

		/* get the name of the key node(s) from the 'value' attribute in key element in data model */
		if ((str = xmlGetProp(keys->nodesetval->nodeTab[i], BAD_CAST "value")) == NULL) {
			continue;
		}

		/* attribute have the form of space-separated list of key nodes */
		/* compare all key node names with specified children */
		for (token = s = (char*) str; token != NULL; s = NULL) {
			token = strtok(s, " ");
			if (token == NULL) {
				break;
			}

			if (xmlStrcmp(BAD_CAST token, children->name) == 0) {
				xmlFree(str);
				return 1;
			}
		}
		xmlFree(str);
	}
	return 0;
}

/**
 * \brief Match 2 elements each other if they are equivalent for NETCONF.
 *
 * Match does not include attributes and children match (only key children are
 * checked). Furthemore, XML node types and namespaces are also checked.
 *
 * Supported XML node types are XML_TEXT_NODE and XML_ELEMENT_NODE.
 *
 * \param[in] node1 First node to compare.
 * \param[in] node2 Second node to compare.
 * \param[in] keys List of key elements from configuration data model.
 *
 * \return 0 - false, 1 - true (matching elements).
 */
int matching_elements(xmlNodePtr node1, xmlNodePtr node2, keyList keys)
{
	xmlNodePtr *keynode_list;
	xmlNodePtr keynode, key;
	xmlChar *key_value = NULL, *keynode_value = NULL;
	char *aux1, *aux2;
	int i, ret;

	assert (node1 != NULL);
	assert (node2 != NULL);

	/* compare text nodes */
	if (node1->type == XML_TEXT_NODE && node2->type == XML_TEXT_NODE) {
		aux1 = nc_clrwspace((char*)(node1->content));
		aux2 = nc_clrwspace((char*)(node2->content));

		if (strcmp(aux1, aux2) == 0) {
			ret = 1;
		} else {
			ret = 0;
		}
		free(aux1);
		free(aux2);
		return ret;
	}

	/* check element types - only element nodes are processed */
	if ((node1->type != XML_ELEMENT_NODE) || (node2->type != XML_ELEMENT_NODE)) {
		return 0;
	}
	/* check element names */
	if (xmlStrcmp(node1->name, node2->name) != 0) {
		return 0;
	}

	/* check element namespace */
	if (nc_nscmp(node1, node2) != 0) {
		return 0;
	}


	if (keys != NULL) {
		if (get_keys(keys, node1, &keynode_list) != EXIT_SUCCESS) {
			/* names matches and it is enough now - TODO namespaces */
			return 1;
		}

		if (keynode_list != NULL) {
			keynode = keynode_list[0];
			for (i = 1; keynode != NULL; i++) {
				/* search in children for the key element */
				key = node2->children;
				while (key != NULL) {
					if (xmlStrcmp(key->name, keynode->name) == 0) {
						/* got key element, now check its value */
						key_value = xmlNodeGetContent(key);
						keynode_value = xmlNodeGetContent(keynode);
						if (xmlStrcmp(keynode_value, key_value) == 0) {
							/* value matches, go for next key if any */
							break; /* while loop */
						} else {
							/* value does not match, go for next element */
							/* this probably would not be necessary, since
							 * there will be no same key element with
							 * different value
							 */
							key = key->next;
						}
					} else {
						/* this was not the key element, try the next one */
						key = key->next;
					}
				}

				/* cleanup for next round */
				if (key_value != NULL) {
					xmlFree(key_value);
				}
				if (keynode_value != NULL) {
					xmlFree(keynode_value);
				}

				if (key == NULL) {
					/* there is no matching node */
					free(keynode_list);
					return 0;
				}

				/* go to the next key if any */
				keynode = keynode_list[i];
			}
			free(keynode_list);
		}
	}

	return 1;
}

/**
 * @brief Go recursively in YIN model and find model's equivalent of the node
 * @param[in] node XML element which we want to find in model
 * @param[in] model Configuration data model (YIN format)
 * @return model's equivalent of the node, NULL if no such element found.
 */
static xmlNodePtr model_recursion(xmlNodePtr node, xmlDocPtr model)
{
	xmlNodePtr mparent, aux;
	xmlChar* name;

	if (node->parent->type != XML_DOCUMENT_NODE) {
		mparent = model_recursion(node->parent, model);
	} else {
		mparent = xmlDocGetRootElement(model);
	}
	if (mparent == NULL) {
		return (NULL);
	}

	for (aux = mparent->children; aux != NULL; aux = aux->next) {
		name = xmlGetNsProp(aux, BAD_CAST "name", BAD_CAST NC_NS_YIN);
		if (name == NULL) {
			continue;
		}

		if (xmlStrcmp(node->name, name) == 0) {
			xmlFree(name);
			return (aux);
		}
		xmlFree(name);
	}

	return (NULL);
}

/**
 * @brief Get default value of the node if a default value defined in the model
 * @param[in] node XML element whose default value we want to get
 * @param[in] model Configuration data model for the document of the given node.
 * @return Default value of the node, NULL if no default value defined or found.
 */
static xmlChar* get_default_value(xmlNodePtr node, xmlDocPtr model)
{
	xmlNodePtr mnode, aux;
	xmlChar* value = NULL;

	mnode = model_recursion(node, model);
	if (mnode == NULL) {
		return (NULL);
	}

	for (aux = mnode->children; aux != NULL; aux = aux->next) {
		if (xmlStrcmp(aux->name, BAD_CAST "default") == 0) {
			value = xmlGetNsProp(aux, BAD_CAST "value", BAD_CAST NC_NS_YIN);
			break;
		}
	}

	return (value);
}

/**
 * \brief Find corresponding equivalent of the given edit node on orig_doc document.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit Element from the edit-config's \<config\>. Its equivalent in
 *                 orig_doc should be found.
 * \param[in] keys List of key elements from configuration data model.
 * \return Found equivalent element, NULL if no such element exists.
 */
static xmlNodePtr find_element_equiv(xmlDocPtr orig_doc, xmlNodePtr edit, keyList keys)
{
	xmlNodePtr orig_parent, node;

	assert(edit != NULL);
	assert(orig_doc != NULL);

	/* go recursively to the root */
	if (edit->parent->type != XML_DOCUMENT_NODE) {
		orig_parent = find_element_equiv(orig_doc, edit->parent, keys);
	} else {
		if (orig_doc->children == NULL) {
			orig_parent = NULL;
		} else {
			orig_parent = orig_doc->children->parent;
		}
	}
	if (orig_parent == NULL) {
		return (NULL);
	}

	/* element check */
	node = orig_parent->children;
	while (node != NULL) {
		/* compare edit and node */
		if (matching_elements(edit, node, keys) == 0) {
			/* non matching nodes */
			node = node->next;
			continue;
		} else {
			/* matching nodes found */
			return (node);
		}
	}

	/* no corresponding node found */
	return (NULL);
}

/**
 * \brief Get the list of elements with specified selected edit-config's operation.
 *
 * \param[in] op edit-config's operation type to search for.
 * \param[in] edit XML document covering edit-config's \<config\> element. The
 *                 elements with specified operation will be searched for in
 *                 this document.
 */
static xmlXPathObjectPtr get_operation_elements(NC_EDIT_OP_TYPE op, xmlDocPtr edit)
{
	xmlXPathContextPtr edit_ctxt = NULL;
	xmlXPathObjectPtr operation_nodes = NULL;
	xmlChar xpath[XPATH_BUFFER];
	char *opstring;

	assert(edit != NULL);

	switch (op) {
	case NC_EDIT_OP_MERGE:
		opstring = NC_EDIT_OP_MERGE_STRING;
		break;
	case NC_EDIT_OP_REPLACE:
		opstring = NC_EDIT_OP_REPLACE_STRING;
		break;
	case NC_EDIT_OP_CREATE:
		opstring = NC_EDIT_OP_CREATE_STRING;
		break;
	case NC_EDIT_OP_DELETE:
		opstring = NC_EDIT_OP_DELETE_STRING;
		break;
	case NC_EDIT_OP_REMOVE:
		opstring = NC_EDIT_OP_REMOVE_STRING;
		break;
	default:
		ERROR( "Unsupported edit operation %d (%s:%d).", op, __FILE__, __LINE__);
		return (NULL);
	}

	/* create xpath evaluation context */
	edit_ctxt = xmlXPathNewContext(edit);
	if (edit_ctxt == NULL) {
		if (edit_ctxt != NULL) { xmlXPathFreeContext(edit_ctxt);}
		ERROR("Creating XPath evaluation context failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (xmlXPathRegisterNs(edit_ctxt, BAD_CAST NC_NS_BASE_ID, BAD_CAST NC_NS_BASE) != 0) {
		xmlXPathFreeContext(edit_ctxt);
		ERROR("Registering namespace for XPath failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (snprintf((char*) xpath, XPATH_BUFFER, "//*[@%s:operation='%s']", NC_NS_BASE_ID, opstring) <= 0) {
		xmlXPathFreeContext(edit_ctxt);
		ERROR("Preparing XPath query failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	operation_nodes = xmlXPathEvalExpression(BAD_CAST xpath, edit_ctxt);

	/* clean up */
	xmlXPathFreeContext(edit_ctxt);

	return (operation_nodes);
}

/**
 * \brief Check edit-config's node operations hierarchy.
 *
 * In case of removing ("remove" and "delete") operations, the supreme operation
 * (including the default operation) cannot be a creating ("create or "replace")
 * operation.
 *
 * In case of creating operations, the supreme operation cannot be a removing
 * operation.
 *
 * \param[in] edit XML node from edit-config's \<config\> which hierarchy
 *                 (supreme operations) will be checked.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and err structure is filled. Zero is
 * returned on success.
 */
static int check_edit_ops_hierarchy(xmlNodePtr edit, NC_EDIT_DEFOP_TYPE defop, struct nc_err **error)
{
	xmlNodePtr parent;
	NC_EDIT_OP_TYPE op, parent_op;

	assert(error != NULL);

	op = get_operation(edit, NC_EDIT_DEFOP_NONE, error);
	if (op == (NC_EDIT_OP_TYPE)NC_EDIT_DEFOP_NONE) {
		/* no operation defined for this node */
		return EXIT_SUCCESS;
	} else if (op == NC_EDIT_OP_ERROR) {
		return EXIT_FAILURE;
	} else if (op == NC_EDIT_OP_DELETE || op == NC_EDIT_OP_REMOVE) {
		if (defop == NC_EDIT_DEFOP_REPLACE) {
			*error = nc_err_new (NC_ERR_OP_FAILED);
			return EXIT_FAILURE;
		}

		/* check parent elements for operation compatibility */
		parent = edit->parent;
		while (parent->type != XML_DOCUMENT_NODE) {
			parent_op = get_operation(parent, NC_EDIT_DEFOP_NONE, error);
			if (parent_op == NC_EDIT_OP_ERROR) {
				return EXIT_FAILURE;
			} else if (parent_op == NC_EDIT_OP_CREATE || parent_op == NC_EDIT_OP_REPLACE) {
				*error = nc_err_new (NC_ERR_OP_FAILED);
				return EXIT_FAILURE;
			}
			parent = parent->parent;
		}
	} else if (op == NC_EDIT_OP_CREATE || op == NC_EDIT_OP_REPLACE) {
		/* check parent elements for operation compatibility */
		parent = edit->parent;
		while (parent->type != XML_DOCUMENT_NODE) {
			parent_op = get_operation(parent, NC_EDIT_DEFOP_NONE, error);
			if (parent_op == NC_EDIT_OP_ERROR) {
				return EXIT_FAILURE;
			} else if (parent_op == NC_EDIT_OP_DELETE || parent_op == NC_EDIT_OP_REMOVE) {
				*error = nc_err_new (NC_ERR_OP_FAILED);
				return EXIT_FAILURE;
			}
			parent = parent->parent;
		}
	}

	return EXIT_SUCCESS;
}

/**
 * \brief Check edit-config's operation rules.
 *
 * In case of "create" operation, if the configuration data exists, an
 * "data-exists" error is generated.
 *
 * In case of "delete" operation, if the configuration data does not exist, an
 * "data-missing" error is generated.
 *
 * Operation hierarchy check check_edit_ops_hierarchy() is also applied.
 *
 * \param[in] op Operation type to check (only "delete" and "create" operation
 * types are valid).
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[in] orig Original configuration document to edit.
 * \param[in] edit XML document covering edit-config's \<config\> element
 * supposed to edit orig configuration data.
 * \param[in] model XML form (YIN) of the configuration data model appropriate
 * to the given repo.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and err structure is filled. Zero is
 * returned on success.
 */
static int check_edit_ops (NC_CHECK_EDIT_OP op, NC_EDIT_DEFOP_TYPE defop, xmlDocPtr orig, xmlDocPtr edit, xmlDocPtr model, struct nc_err **error)
{
	xmlXPathObjectPtr operation_nodes = NULL;
	xmlNodePtr node_to_process = NULL, n;
	keyList keys;
	xmlChar *defval = NULL, *value = NULL;
	int i;

	assert(orig != NULL);
	assert(edit != NULL);
	assert(error != NULL);

	keys = get_keynode_list(model);

	operation_nodes = get_operation_elements((NC_EDIT_OP_TYPE) op, edit);
	if (operation_nodes == NULL) {
		*error = nc_err_new (NC_ERR_OP_FAILED);
		return EXIT_FAILURE;
	}

	if (xmlXPathNodeSetIsEmpty(operation_nodes->nodesetval)) {
		xmlXPathFreeObject(operation_nodes);
		return EXIT_SUCCESS;
	}

	*error = NULL;
	for (i = 0; i < operation_nodes->nodesetval->nodeNr; i++) {
		node_to_process = operation_nodes->nodesetval->nodeTab[i];

		if (check_edit_ops_hierarchy(node_to_process, defop, error) != EXIT_SUCCESS) {
			xmlXPathFreeObject(operation_nodes);
			return EXIT_FAILURE;
		}

		/* \todo namespace handlings */
		n = find_element_equiv(orig, node_to_process, keys);
		if (op == NC_CHECK_EDIT_DELETE && n == NULL) {
			if (ncdflt_get_basic_mode() == NCWD_MODE_ALL) {
				/* A valid 'delete' operation attribute for a
				 * data node that contains its schema default
				 * value MUST succeed, even though the data node
				 * is immediately replaced by the server with
				 * the default value.
				 */
				defval = get_default_value(node_to_process, model);
				if (defval == NULL) {
					/* no default value for this node */
					*error = nc_err_new (NC_ERR_DATA_MISSING);
					break;
				}
				value = xmlNodeGetContent(node_to_process);
				if (value == NULL) {
					*error = nc_err_new (NC_ERR_DATA_MISSING);
					break;
				}
				if (xmlStrcmp(defval, value) != 0) {
					/* node do not contain default value */
					*error = nc_err_new (NC_ERR_DATA_MISSING);
					break;
				} else {
					/* remove delete operation - it is valid
					 * but there is no reason to really
					 * perform it
					 */
					xmlUnlinkNode(node_to_process);
					xmlFreeNode(node_to_process);
				}
				xmlFree(defval); defval = NULL;
				xmlFree(value); value = NULL;
			} else {
				*error = nc_err_new (NC_ERR_DATA_MISSING);
				break;
			}
		} else if (op == NC_CHECK_EDIT_CREATE  && n != NULL) {
			if (ncdflt_get_basic_mode() == NCWD_MODE_TRIM) {
				/* A valid 'create' operation attribute for a
				 * data node that has a schema default value
				 * defined MUST succeed.
				 */
				defval = get_default_value(node_to_process, model);
				if (defval == NULL) {
					/* no default value for this node */
					*error = nc_err_new (NC_ERR_DATA_EXISTS);
					break;
				}
				value = xmlNodeGetContent(node_to_process);
				if (value == NULL) {
					*error = nc_err_new (NC_ERR_DATA_EXISTS);
					break;
				}
				if (xmlStrcmp(defval, value) != 0) {
					/* node do not contain default value */
					*error = nc_err_new (NC_ERR_DATA_MISSING);
					break;
				} else {
					/* remove old node in configuration to
					 * allow recreate it by the new one with
					 * the default value
					 */
					xmlUnlinkNode(n);
					xmlFreeNode(n);
				}
				xmlFree(defval); defval = NULL;
				xmlFree(value); value = NULL;

			} else {
				*error = nc_err_new (NC_ERR_DATA_EXISTS);
				break;
			}
		}
	}
	xmlXPathFreeObject(operation_nodes);
	if (defval != NULL) { xmlFree(defval);}
	if (value != NULL) { xmlFree(value);}

	if (*error != NULL) {
		return (EXIT_FAILURE);
	} else {
		return EXIT_SUCCESS;
	}
}

/**
 * \brief Perform edit-config's "delete" operation on the selected node.
 *
 * \param[in] node XML node from the configuration data to delete.
 * \return Zero on success, non-zero otherwise.
 */
static int edit_delete (xmlNodePtr node)
{
	assert(node != NULL);

	VERB("Deleting node %s (%s:%d)", (char*)node->name, __FILE__, __LINE__);
	if (node != NULL) {
		xmlUnlinkNode(node);
		xmlFreeNode(node);
	}

	return EXIT_SUCCESS;
}

/**
 * \brief Perform edit-config's "remove" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * specified "remove" operation.
 * \param[in] keys  List of key elements from configuration data model.
 *
 * \return Zero on success, non-zero otherwise.
 */
static int edit_remove (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr old;

	old = find_element_equiv(orig_doc, edit_node, keys);

	/* remove the node from the edit document */
	edit_delete(edit_node);

	if (old == NULL) {
		return EXIT_SUCCESS;
	} else {
		/* remove edit node's equivalent from the original document */
		return edit_delete(old);
	}
}

/**
 * \brief Recursive variant of edit_create() function to create missing parent path of the node to be created.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the missing parent chain of the element to
 *                      create. If there is no equivalent node in the original
 *                      document, it is created.
 * \param[in] keys  List of key elements from configuration data model.
 *
 * \return Zero on success, non-zero otherwise.
 */
static xmlNodePtr edit_create_recursively (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr retval = NULL;
	xmlNodePtr parent = NULL;

	retval = find_element_equiv(orig_doc, edit_node, keys);
	if (retval == NULL) {
		parent = edit_create_recursively(orig_doc, edit_node->parent, keys);
		if (parent == NULL) {
			return NULL;
		}
		VERB("Creating parent %s (%s:%d)", (char*)edit_node->name, __FILE__, __LINE__);
		retval = xmlAddChild(parent, xmlCopyNode(edit_node, 0));
	}
	return retval;
}

/**
 * \brief Perform edit-config's "create" operation.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * specified "create" operation.
 * \param[in] keys  List of key elements from configuration data model.
 *
 * \return Zero on success, non-zero otherwise.
 */
static int edit_create (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr parent = NULL;

	assert(orig_doc != NULL);
	assert(edit_node != NULL);

	if (edit_node->parent->type != XML_DOCUMENT_NODE) {
		parent = edit_create_recursively(orig_doc, edit_node->parent, keys);
		if (parent == NULL) {
			return EXIT_FAILURE;
		}
	}
	/* remove operation attribute */
	xmlRemoveProp(xmlHasNsProp(edit_node, BAD_CAST NC_EDIT_ATTR_OP, BAD_CAST NC_NS_BASE));

	/* create new element in configuration data as a copy of the element from the edit-config */
	VERB("Creating node %s (%s:%d)", (char*)edit_node->name, __FILE__, __LINE__);
	if (parent == NULL) {
		if (orig_doc->children == NULL) {
			xmlDocSetRootElement(orig_doc, xmlCopyNode(edit_node, 1));
		} else {
			xmlAddSibling(orig_doc->children, xmlCopyNode(edit_node, 1));
		}
	} else {
		if (xmlAddChild(parent, xmlCopyNode(edit_node, 1)) == NULL) {
			return EXIT_FAILURE;
		}
	}

	/* remove the node from the edit document */
	edit_delete(edit_node);

	return EXIT_SUCCESS;
}

/**
 * \brief Perform edit-config's "replace" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * specified "replace" operation.
 * \param[in] keys  List of key elements from configuration data model.
 *
 * \return Zero on success, non-zero otherwise.
 */
static int edit_replace (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr old;

	old = find_element_equiv(orig_doc, edit_node, keys);
	if (old == NULL) {
		/* node to be replaced doesn't exist, so create new configuration data */
		return edit_create (orig_doc, edit_node, keys);
	} else {
		/* remove operation attribute */
		xmlRemoveProp(xmlHasNsProp(edit_node, BAD_CAST NC_EDIT_ATTR_OP, BAD_CAST NC_NS_BASE));

		/* replace old configuration data with new data */
		VERB("Replacing node %s (%s:%d)", (char*)old->name, __FILE__, __LINE__);
		if (xmlReplaceNode(old, xmlCopyNode(edit_node, 1)) == NULL) {
			return EXIT_FAILURE;
		}
		xmlFreeNode (old);

		/* remove the node from the edit document */
		edit_delete(edit_node);

		return EXIT_SUCCESS;
	}
}

static int edit_merge_recursively(xmlNodePtr orig_node, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr children, aux;
	int retval;

	/* process leaf text nodes - even if we are merging, leaf text nodes are
	 * actually replaced by data specified by the edit configuration data
	 */
	if (edit_node->type == XML_TEXT_NODE) {
		if (orig_node->type == XML_TEXT_NODE) {
			if (xmlReplaceNode(orig_node, xmlCopyNode(edit_node,1)) == NULL) {
				ERROR("Replacing text nodes when merging failed (%s:%d)", __FILE__, __LINE__);
				return EXIT_FAILURE;
			}
			xmlFreeNode(orig_node);
		}
	}

	children = edit_node->children;
	while (children != NULL) {
		/* skip checks if the node is text */
		if (children->type == XML_TEXT_NODE) {
			/* find text element to children */
			aux = orig_node->children;
			while (aux != NULL && aux->type != XML_TEXT_NODE) {
				aux = aux->next;
			}
		} else {
			/* skip key elements from merging */
			if (is_key(edit_node, children, keys) != 0) {
				children = children->next;
				continue;
			}
			/* skip comments */
			if (children->type == XML_COMMENT_NODE) {
				children = children->next;
				continue;
			}

			/* find matching element to children */
			aux = orig_node->children;
			while (aux != NULL && matching_elements(aux, children, keys) == 0) {
				aux = aux->next;
			}
		}

		if (aux == NULL) {
			/*
			 * there is no equivalent element of the children in the
			 * original configuration data, so create it as new
			 */
			if (xmlAddChild(orig_node, xmlCopyNode(children, 1)) == NULL) {
				ERROR("Adding missing nodes when merging failed (%s:%d)", __FILE__, __LINE__);
				return EXIT_FAILURE;
			}
			VERB("Adding missing node %s while merging (%s:%d)", (char*)children->name, __FILE__, __LINE__);
		} else {
			VERB("Merging node %s (%s:%d)", (char*)children->name, __FILE__, __LINE__);
			/* go recursive */
			retval = edit_merge_recursively(aux, children, keys);
			if (retval != EXIT_SUCCESS) {
				return EXIT_FAILURE;
			}
		}

		children = children->next;
	}

	return EXIT_SUCCESS;
}

int edit_merge (xmlDocPtr orig_doc, xmlNodePtr edit_node, keyList keys)
{
	xmlNodePtr orig_node;
	xmlNodePtr aux, children;

	assert(edit_node != NULL);

	/* here can be processed only elements or document root */
	if (edit_node->type != XML_ELEMENT_NODE) {
		ERROR("Merge request for unsupported XML node types (%s:%d)", __FILE__, __LINE__);
		return EXIT_FAILURE;
	}

	VERB("Merging node %s (%s:%d)", (char*)edit_node->name, __FILE__, __LINE__);
	orig_node = find_element_equiv(orig_doc, edit_node, keys);
	if (orig_node == NULL) {
		return edit_create(orig_doc, edit_node, keys);
	}

	children = edit_node->children;
	while (children != NULL) {
		if (is_key(edit_node, children, keys) != 0) {
			/* skip key elements from merging */
			children = children->next;
			continue;
		}

		aux = find_element_equiv(orig_doc, children, keys);
		if (aux == NULL) {
			/*
			 * there is no equivalent element of the children in the
			 * original configuration data, so create it as new
			 */
			if (xmlAddChild(orig_node, xmlCopyNode(children, 1)) == NULL) {
				ERROR("Adding missing nodes when merging failed (%s:%d)", __FILE__, __LINE__);
				return EXIT_FAILURE;
			}
		} else {
			/* go recursive */
			if (edit_merge_recursively(aux, children, keys) != EXIT_SUCCESS) {
				return EXIT_FAILURE;
			}
		}

		children = children->next;
	}
	/* remove the node from the edit document */
	edit_delete(edit_node);

	return EXIT_SUCCESS;
}

/**
 * \brief Perform all edit-config's operations specified in edit_doc document.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_doc XML document covering edit-config's \<config\> element
 *                     supposed to edit orig_doc configuration data.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[in] keys  List of key elements from configuration data model.
 * \param[out] err NETCONF error structure.
 *
 * \return On error, non-zero is returned and err structure is filled. Zero is
 *         returned on success.
 */
static int edit_operations (xmlDocPtr orig_doc, xmlDocPtr edit_doc, NC_EDIT_DEFOP_TYPE defop, keyList keys, struct nc_err **error)
{
	xmlXPathObjectPtr nodes;
	int i;
	xmlNodePtr orig_node, edit_node;

	assert(error != NULL);

	/* default replace */
	if (defop == NC_EDIT_DEFOP_REPLACE) {
		/* replace whole document */
		edit_replace(orig_doc, edit_doc->children, keys);
	}

	/* delete operations */
	nodes = get_operation_elements(NC_EDIT_OP_DELETE, edit_doc);
	if (nodes != NULL) {
		if (xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
			xmlXPathFreeObject(nodes);
		} else {
			/* something to delete */
			for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
				edit_node = nodes->nodesetval->nodeTab[i];
				orig_node = find_element_equiv(orig_doc, edit_node, keys);
				if (orig_node == NULL) {
					goto error;
				}
				/* remove the node from the edit document */
				edit_delete(edit_node);
				/* remove the edit node's equivalent from the original document */
				edit_delete(orig_node);
			}
		}
	}

	/* remove operations */
	nodes = get_operation_elements(NC_EDIT_OP_REMOVE, edit_doc);
	if (nodes != NULL) {
		if (xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
			xmlXPathFreeObject(nodes);

		} else {
			/* something to remove */
			for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
				edit_node = nodes->nodesetval->nodeTab[i];
				edit_remove(orig_doc, edit_node, keys);
			}
		}
	}

	/* replace operations */
	nodes = get_operation_elements(NC_EDIT_OP_REPLACE, edit_doc);
	if (nodes != NULL) {
		if (xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
			xmlXPathFreeObject(nodes);

		} else {
			/* something to replace */
			for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
				edit_node = nodes->nodesetval->nodeTab[i];
				edit_replace(orig_doc, edit_node, keys);
			}
		}
	}

	/* create operations */
	nodes = get_operation_elements(NC_EDIT_OP_CREATE, edit_doc);
	if (nodes != NULL) {
		if (xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
			xmlXPathFreeObject(nodes);

		} else {
			/* something to create */
			for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
				edit_node = nodes->nodesetval->nodeTab[i];
				edit_create(orig_doc, edit_node, keys);
			}
		}
	}

	/* merge operations */
	nodes = get_operation_elements(NC_EDIT_OP_MERGE, edit_doc);
	if (nodes != NULL) {
		if (xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
			xmlXPathFreeObject(nodes);

		} else {
			/* something to create */
			for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
				edit_node = nodes->nodesetval->nodeTab[i];
				if (edit_node != NULL) {
					edit_merge(orig_doc, edit_node, keys);
				}
			}
		}
	}

	/* default merge */
	if (defop == NC_EDIT_DEFOP_MERGE) {
		/* replace whole document */
		if (edit_doc->children != NULL) {
			edit_merge(orig_doc, edit_doc->children, keys);
		}
	}


	return EXIT_SUCCESS;

error:
	*error = nc_err_new (NC_ERR_OP_FAILED);
	return EXIT_FAILURE;
}

static int compact_edit_operations_recursively (xmlNodePtr node, NC_EDIT_OP_TYPE supreme_op)
{
	NC_EDIT_OP_TYPE op;
	xmlNodePtr children;
	int ret;

	op = get_operation(node, NC_EDIT_DEFOP_NONE, NULL);
	switch((int)op) {
	case NC_EDIT_OP_ERROR:
		return EXIT_FAILURE;
		break;
	case 0:
		/* no operation defined -> go recursively */
		break;
	default:
		/* any operation specified */
		if (op == supreme_op) {
			/* operation duplicity -> remove subordinate duplicated operation */
			/* remove operation attribute */
			xmlRemoveProp(xmlHasNsProp(node, BAD_CAST NC_EDIT_ATTR_OP, BAD_CAST NC_NS_BASE));
		}
		break;
	}

	/* go recursive */
	for (children = node->children; children != NULL; children = children->next) {
		ret = compact_edit_operations_recursively(children, op);
		if (ret == EXIT_FAILURE) {
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int compact_edit_operations (xmlDocPtr edit_doc)
{
	if (edit_doc == NULL) {
		return EXIT_FAILURE;
	}

	/* to start recursive check, use 0 as NC_OP_TYPE_NONE which actually does not exist */
	return compact_edit_operations_recursively(edit_doc->children, 0);
}

/**
 * \brief Perform edit-config changes according to given parameters
 *
 * \param[in] repo XML document to change (target NETCONF repository).
 * \param[in] edit content of the edit-config's \<config\> element as XML
 * document defining changes to perform.
 * \param[in] model XML form (YIN) of the configuration data model appropriate to the given repo.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[in] errop NETCONF edit-config's error option defining reactions to an error.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and err structure is filled. Zero is
 * returned on success.
 */
int edit_config(xmlDocPtr repo, xmlDocPtr edit, xmlDocPtr model, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE UNUSED(errop), struct nc_err **error)
{
	if (repo == NULL || edit == NULL) {
		return (EXIT_FAILURE);
	}

	keyList keys;
	keys = get_keynode_list(model);

	/* check operations */
	if (check_edit_ops(NC_CHECK_EDIT_DELETE, defop, repo, edit, model, error) != EXIT_SUCCESS) {
		goto error_cleanup;
	}
	if (check_edit_ops(NC_CHECK_EDIT_CREATE, defop, repo, edit, model, error) != EXIT_SUCCESS) {
		goto error_cleanup;
	}

	if (compact_edit_operations(edit) != EXIT_SUCCESS) {
		ERROR("Compacting edit-config operations failed.");
		if (error != NULL) {
			*error = nc_err_new (NC_ERR_OP_FAILED);
		}
		goto error_cleanup;
	}

	/* perform operations */
	if (edit_operations(repo, edit, defop, keys, error) != EXIT_SUCCESS) {
		goto error_cleanup;
	}

	/* with defaults capability */
	if (ncdflt_get_basic_mode() == NCWD_MODE_TRIM) {
		/* server work in trim basic mode and therefore all default
		 * values must be removed from the datastore.
		 */
		ncdflt_default_values(repo, model, NCWD_MODE_TRIM);
	}

	if (keys != NULL) {
		keyListFree(keys);
	}

	return EXIT_SUCCESS;

error_cleanup:

	if (keys != NULL) {
		keyListFree(keys);
	}

	return EXIT_FAILURE;
}

