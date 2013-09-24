#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "../datastore.h"
#include "yinparser.h"

int get_node_namespace(const char * ns_mapping[], xmlNodePtr node, char ** prefix, char ** uri)
{
	int i;

	if (((*uri) = (char*)xmlGetNsProp(node, BAD_CAST "ns", BAD_CAST "libnetconf")) == NULL) {
		return(EXIT_FAILURE);
	} else {
		for (i=0; ns_mapping[2*i] != NULL; i++) {
			if (strcmp(ns_mapping[2*i+1], (*uri)) == 0) {
				(*prefix) = strdup(ns_mapping[2*i]);
				break;
			}
		}
		if ((*prefix) == NULL) {
			return(EXIT_FAILURE);
		}
	}

	return(EXIT_SUCCESS);
}

struct model_tree * yinmodel_parse_recursive (xmlNodePtr model_node, const char * ns_mapping[], struct model_tree * parent, int *children_count)
{
	struct model_tree * children, * choice;
	xmlNodePtr int_tmp, list_tmp, model_tmp = model_node->children;
	int count = 0, case_count, config;
	char * keys, * key, * config_text;
	xmlChar *value;

	children = NULL;
	while (model_tmp) {
		count++;
		children = realloc (children, sizeof (struct model_tree) * count);
		/* first check if node holds configuration */
		int_tmp = model_tmp->children;
		config = 1;
		while (int_tmp) {
			if (xmlStrEqual (int_tmp->name, BAD_CAST "config")) {
				config_text = (char*)xmlNodeGetContent(int_tmp);
				if (strcasecmp (config_text, "false") == 0 || strcasecmp (config_text, "0") == 0) {
					config = 0;
				}
				free (config_text);
				break;
			}
			int_tmp = int_tmp->next;
		}
	
		if (!config) {
			count--;
			model_tmp = model_tmp->next;
			continue;
		}
		/* check if node is in other namespace */
		if (get_node_namespace(ns_mapping, model_tmp, &children[count-1].ns_prefix, &children[count-1].ns_uri)) {
			/* or inherit from parent */
			children[count-1].ns_prefix = strdup(parent->ns_prefix);
			children[count-1].ns_uri = strdup(parent->ns_uri);
		}
		children[count-1].name = (char*)xmlGetProp (model_tmp, BAD_CAST "name");
		children[count-1].keys_count = 0;
		children[count-1].keys = NULL;
		if (xmlStrEqual(model_tmp->name, BAD_CAST "container")) {
			children[count-1].type = YIN_TYPE_CONTAINER;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, ns_mapping, &children[count-1], &children[count-1].children_count);
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "leaf")) {
			children[count-1].type = YIN_TYPE_LEAF;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "leaf-list")) {
			children[count-1].type = YIN_TYPE_LEAFLIST;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
			children[count-1].ordering = YIN_ORDER_SYSTEM;
			list_tmp = model_tmp->children;
			while (list_tmp) {
				if (xmlStrEqual(list_tmp->name, BAD_CAST "ordered-by")) {
					value = xmlGetProp(list_tmp, BAD_CAST "value");
					if (xmlStrEqual(value, BAD_CAST "user")) {
						children[count-1].ordering = YIN_ORDER_USER;
					}
					xmlFree(value);
					break;
				}
				list_tmp = list_tmp->next;
			}
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "list")) {
			children[count-1].type = YIN_TYPE_LIST;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
			children[count-1].ordering = YIN_ORDER_SYSTEM;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, ns_mapping, &children[count-1], &children[count-1].children_count);
			list_tmp = model_tmp->children;
			while (list_tmp) {
				if (xmlStrEqual(list_tmp->name, BAD_CAST "ordered-by")) {
					value = xmlGetProp(list_tmp, BAD_CAST "value");
					if (xmlStrEqual(value, BAD_CAST "user")) {
						children[count-1].ordering = YIN_ORDER_USER;
					}
					xmlFree(value);
				} else if (xmlStrEqual(list_tmp->name, BAD_CAST "key")) {
					keys = (char*)xmlGetProp (list_tmp, BAD_CAST "value");
					children[count-1].keys_count = 0;
					children[count-1].keys = NULL;
					key = strtok (keys, " ");
					while (key) {
						children[count-1].keys_count++;
						children[count-1].keys = realloc (children[count-1].keys, sizeof (char*) * children[count-1].keys_count);
						children[count-1].keys[children[count-1].keys_count-1] = strdup (key);
						key = strtok (NULL, " ");
					}
					free (keys);
				}
				list_tmp = list_tmp->next;
			}
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "choice")) {
			children[count-1].type = YIN_TYPE_CHOICE;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, ns_mapping, &children[count-1], &children[count-1].children_count);
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "anyxml")) {
			children[count-1].type = YIN_TYPE_ANYXML;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "case")) {
			choice = yinmodel_parse_recursive (model_tmp->children, ns_mapping, &children[count-1], &case_count);
			children = realloc (children, sizeof (struct model_tree) * (case_count+count));
			memcpy (&children[count-1], choice, case_count*sizeof(struct model_tree));
			count += case_count;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "uses")) {
			/* search through groupings in module and submodules */
			/* place content here */
			/* TODO: maybe groupings and submodules should be accessible somewhere :-D */
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "augment")) {
			children[count-1].type = YIN_TYPE_AUGMENT;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, ns_mapping, &children[count-1], &children[count-1].children_count);
		} else {
			free (children[count-1].name);
			free (children[count-1].ns_prefix);
			free (children[count-1].ns_uri);
			count--;
			model_tmp = model_tmp->next;
			continue;
			/* warning: unsupported or elsewhere processed (key,config) node type found */
		}

		model_tmp = model_tmp->next;
	}

	(*children_count) = count;
	return children;
}

void yinmodel_free_recursive (struct model_tree * yin)
{
	int i;

	if (yin == NULL) {
		return;
	}

	free(yin->ns_prefix);
	free(yin->ns_uri);

	for (i=0; i<yin->keys_count; i++) {
		free(yin->keys[i]);
	}
	free (yin->keys);

	for (i=0; i<yin->children_count; i++) {
		yinmodel_free_recursive (&yin->children[i]);
	}
	free (yin->children);

	free (yin->name);
}

void yinmodel_free (struct model_tree * yin)
{
	if (yin != NULL) {
		yinmodel_free_recursive (yin);
		free (yin);
	}
}

struct model_tree * yinmodel_parse (xmlDocPtr model_doc, const char * ns_mapping[])
{
	xmlNodePtr model_root, model_top = NULL, model_tmp;
	struct model_tree * yin, * yin_act;
	int config, i;
	char * config_text;

	if ((model_root = xmlDocGetRootElement (model_doc)) == NULL) {
		/* cant get model root */
		return NULL;
	}

	if (!xmlStrEqual (model_root->name, BAD_CAST "module")) {
		/* model root != module => invalid model */
		return NULL;
	}

	yin = calloc (1, sizeof (struct model_tree));
	yin->type = YIN_TYPE_MODULE;
	yin->name = (char*)xmlGetProp (model_root, BAD_CAST "name");


	/* find namespace, prefix and root of configuration data for this module */
	model_tmp = model_root->children;
	while (model_tmp) {
		if (xmlStrEqual(model_tmp->name, BAD_CAST "namespace")) {
			yin->ns_uri = (char*)xmlGetProp(model_tmp, BAD_CAST "uri");
			for (i=0; ns_mapping[2*i] != NULL; i++) {
				if (strcmp(ns_mapping[2*i+1], yin->ns_uri) == 0) {
					yin->ns_prefix = strdup(ns_mapping[2*i]);
					break;
				}
			}
			if (yin->ns_prefix == NULL) {
				yinmodel_free(yin);
				return(NULL);
			}
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "container")) {
			model_top = model_tmp;
		}

		model_tmp = model_tmp->next;
	}

	/* model contains no data (probably only typedefs, rpcs, notofications, ...)*/
	if (model_top == NULL) {
		return yin;
	}

	/* find model top, will be last child of module */
	model_tmp = model_top->children;
	config = 1;
	while (model_tmp) {
		if (xmlStrEqual (model_tmp->name, BAD_CAST "config")) {
			config_text = (char*)xmlNodeGetContent(model_tmp);
			if (strcasecmp (config_text, "false") == 0) {
				config = 0;
			}
			free (config_text);
			break;
		}
		model_tmp = model_tmp->next;
	}

	if (config) {
		yin->children_count++;
		yin->children = realloc (yin->children, yin->children_count * sizeof (struct model_tree));
		yin->children[yin->children_count-1].type = YIN_TYPE_CONTAINER;
		yin->children[yin->children_count-1].name = (char*)xmlGetProp (model_top, BAD_CAST "name");
		yin->children[yin->children_count-1].keys_count = 0;
		yin->children[yin->children_count-1].keys = NULL;
		yin->children[yin->children_count-1].children_count = 0;
		yin->children[yin->children_count-1].children = NULL;
		if (get_node_namespace(ns_mapping, model_top, &yin->children[yin->children_count-1].ns_prefix, &yin->children[yin->children_count-1].ns_uri)) {
			yin->children[yin->children_count-1].ns_prefix = strdup(yin->ns_prefix);
			yin->children[yin->children_count-1].ns_uri = strdup(yin->ns_uri);
		}
		yin_act = &yin->children[yin->children_count-1];
		yin_act->children = yinmodel_parse_recursive (model_top, ns_mapping, yin_act, &yin_act->children_count);
	}

	return yin;
}
