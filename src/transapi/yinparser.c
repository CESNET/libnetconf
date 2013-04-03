#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "yinparser.h"

struct yinmodel * yinmodel_parse_recursive (xmlNodePtr model_node, int *children_count)
{
	struct yinmodel * children, * choice;
	xmlNodePtr int_tmp, list_tmp, model_tmp = model_node->children;
	int count = 0, case_count, config;
	char * keys, * key, * config_text;

	children = NULL;
	while (model_tmp) {
		count++;
		children = realloc (children, sizeof (struct yinmodel) * count);
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
		children[count-1].name = (char*)xmlGetProp (model_tmp, BAD_CAST "name");
		children[count-1].keys_count = 0;
		children[count-1].keys = NULL;
		if (xmlStrEqual(model_tmp->name, BAD_CAST "container")) {
			children[count-1].type = YIN_TYPE_CONTAINER;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, &children[count-1].children_count);
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "leaf")) {
			children[count-1].type = YIN_TYPE_LEAF;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "leaf-list")) {
			children[count-1].type = YIN_TYPE_LEAFLIST;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "list")) {
			children[count-1].type = YIN_TYPE_LIST;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, &children[count-1].children_count);
			list_tmp = model_tmp->children;
			while (list_tmp) {
				if (xmlStrEqual(list_tmp->name, BAD_CAST "key")) {
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

					break;
				}
				list_tmp = list_tmp->next;
			}
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "choice")) {
			children[count-1].type = YIN_TYPE_CHOICE;
			children[count-1].children = yinmodel_parse_recursive (model_tmp, &children[count-1].children_count);
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "anyxml")) {
			children[count-1].type = YIN_TYPE_ANYXML;
			children[count-1].children = NULL;
			children[count-1].children_count = 0;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "case")) {
			choice = yinmodel_parse_recursive (model_tmp->children, &case_count);
			children = realloc (children, sizeof (struct yinmodel) * (case_count+count));
			memcpy (&children[count-1], choice, case_count*sizeof(struct yinmodel));
			count += case_count;
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "uses")) {
			/* search through groupings in module and submodules */
			/* place content here */
			/* TODO: maybe groupings and submodules should be accessible somewhere :-D */
		} else if (xmlStrEqual(model_tmp->name, BAD_CAST "augment")) {
			/* find augmented node take it to main module and add augment into it */
		} else {
			free (children[count-1].name);
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

void yinmodel_free_recursive (struct yinmodel * yin)
{
	int i;

	if (yin == NULL) {
		return;
	}

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

void yinmodel_free (struct yinmodel * yin)
{
	yinmodel_free_recursive (yin);

	free (yin);
}

struct yinmodel * yinmodel_parse (xmlDocPtr model_doc)
{
	xmlNodePtr model_root, model_top, model_tmp, import_node, grouping_node;
	struct yinmodel * yin, * yin_act;
	int config;
	char * config_text;

	if ((model_root = xmlDocGetRootElement (model_doc)) == NULL) {
		/* cant get model root */
		return NULL;
	}

	if (!xmlStrEqual (model_root->name, BAD_CAST "module")) {
		/* model root != module => invalid model */
		return NULL;
	}

	yin = calloc (1, sizeof (struct yinmodel));
	yin->type = YIN_TYPE_MODULE;
	yin->name = (char*)xmlGetProp (model_root, BAD_CAST "name");

	/* process all imported models */
	import_node = model_root->children;
	while (import_node) {
		if (xmlStrEqual(import_node->name, BAD_CAST "import")) {
			/* create child import with prefix found here */
			/* find yin file, load XML and call yinmodel_parse() */
			/* save as import child */
		}
		import_node = import_node->next;
	}

	/* process all defined groupings */
	grouping_node = model_root->children;
	while (grouping_node) {
		if (xmlStrEqual(grouping_node->name, BAD_CAST "grouping")) {
			/* process simmilary to container */
			/* dont forget to add processing of "uses" to yinmodel_parse_recursive() */
			/* then save to module childrens */
		}
		grouping_node = grouping_node->next;
	}

	/* find top level container */
	model_top = model_root->children;
	while (model_top) {
		if (xmlStrEqual(model_top->name, BAD_CAST "container")) {
			break;
		}
		model_top = model_top->next;
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
		yin->children = realloc (yin->children, yin->children_count * sizeof (struct yinmodel));
		yin->children[yin->children_count-1].type = YIN_TYPE_CONTAINER;
		yin->children[yin->children_count-1].name = (char*)xmlGetProp (model_top, BAD_CAST "name");
		yin->children[yin->children_count-1].keys_count = 0;
		yin->children[yin->children_count-1].keys = NULL;
		yin->children[yin->children_count-1].children_count = 0;
		yin->children[yin->children_count-1].children = NULL;

		yin_act = &yin->children[yin->children_count-1];
		yin_act->children = yinmodel_parse_recursive (model_top, &yin_act->children_count);
	}

	return yin;
}
