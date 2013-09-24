#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "xmldiff.h"
#include "transapi_xml.h"
#include "yinparser.h"
#include "transapi_internal.h"

/* adds a priority into priority buffer structure */
void xmldiff_add_priority(int prio, struct xmldiff_prio** prios) {
	if (*prios == NULL) {
		*prios = malloc(sizeof(struct xmldiff_prio));
		(*prios)->used = 0;
		(*prios)->alloc = 10;
		(*prios)->values = malloc(10 * sizeof(int));
	} else if ((*prios)->used == (*prios)->alloc) {
		(*prios)->alloc *= 2;
		(*prios)->values = realloc((*prios)->values, (*prios)->alloc * sizeof(int));
	}

	(*prios)->values[(*prios)->used] = prio;
	(*prios)->used += 1;
}

/* appends two priority structures, handy for merging all children priorities into one for the parent */
void xmldiff_merge_priorities(struct xmldiff_prio** old, struct xmldiff_prio* new) {
	if (new == NULL || *old == NULL) {
		if (*old == NULL) {
			*old = new;
		}
		return;
	}

	if ((*old)->alloc - (*old)->used < new->used) {
		(*old)->alloc *= 2;
		(*old)->values = realloc((*old)->values, (*old)->alloc * sizeof(int));
	}

	memcpy((*old)->values+(*old)->used, new->values, new->used * sizeof(int));
	(*old)->used += new->used;

	free(new->values);
	free(new);
}

/* the recursive core of xmldiff_set_priorities() function */
struct xmldiff_prio* xmldiff_set_priority_recursive(struct xmldiff_tree* tree, struct transapi_xml_data_callbacks* calls) {
	int i, min_prio, children_count = 0, children_without_callback = 0;
	struct xmldiff_prio* priorities = NULL, *tmp_prio;
	struct xmldiff_tree* child;

	/* First search for the callbacks of our children */
	child = tree->children;
	while (child != NULL) {
		++children_count;
		tmp_prio = xmldiff_set_priority_recursive(child, calls);
		if (tmp_prio == NULL) {
			++children_without_callback;
		}
		xmldiff_merge_priorities(&priorities, tmp_prio);
		child = child->next;
	}

	/* Fix XMLDIFF_OP */
	if (tree->op & XMLDIFF_CHAIN) {
		if (children_count == 0) {
			/* Cannot happen */
		} else if (children_without_callback == 0) {
			/* All of our children have a callback -> XMLDIFF_CHAIN stays */
		} else if (children_count > children_without_callback) {
			/* Some children have a callback, some don't -> XMLDIFF_CHAIN | XMLDIFF_MOD */
			tree->op |= XMLDIFF_MOD;
		} else { /* (children == children_without_callback) */
			/* No child has a callback -> XMLDIFF_MOD */
			tree->op = XMLDIFF_MOD;
		}
	} else { /* XMLDIFF_ADD or XMLDIFF_REM */
		if (children_count == 0) {
			/* XMLDIFF_OP is correct */
		} else if (children_without_callback == 0) {
			/* All of our children have a callback -> XMLDIFF_CHAIN | previous op */
			tree->op |= XMLDIFF_CHAIN;
		} else if (children_count > children_without_callback) {
			/* Some children have a callback, chain should be set -> XMLDIFF_CHAIN | previous op */
			tree->op |= XMLDIFF_CHAIN;
		} else { /* (children == children_without_callback) */
			/* No child has a callback -> only the previous op */
		}
	}

	/* Search for the callback */
	for (i = 0; i < calls->callbacks_count; ++i) {
		if (strcmp(calls->callbacks[i].path, tree->path) == 0) {
			break;
		}
	}

	if (i == calls->callbacks_count && priorities != NULL) {
		/* We do not have a callback, so we use the lowest priority from our children callbacks */
		min_prio = priorities->values[0];
		for (i = 1; i < priorities->used; ++i) {
			if (priorities->values[i] < min_prio) {
				min_prio = priorities->values[i];
			}
		}

		tree->priority = min_prio;

		/* Save our priority */
		xmldiff_add_priority(min_prio, &priorities);
	} else if (i < calls->callbacks_count) {
		/* We have a callback */
		tree->callback = true;
		tree->priority = i+1;

		/* Save our priority */
		xmldiff_add_priority(i+1, &priorities);
	} else {
		/* We do not have a callback and neither does any of our children, maybe our parent does */
	}

	return priorities;
}

int xmldiff_set_priorities(struct xmldiff_tree* tree, void* callbacks) {
	struct transapi_xml_data_callbacks* calls = callbacks;
	struct xmldiff_prio* ret;

	ret = xmldiff_set_priority_recursive(tree, calls);

	/* There is no callback to call for the configuration change, that probably should not happen */
	if (ret == NULL) {
		return EXIT_FAILURE;
	}

	free(ret->values);
	free(ret);
	return EXIT_SUCCESS;
}

/**
 * @brief Destroy and free whole xmldiff_tree structure
 *
 * @param diff	pointer to xmldiff structure
 */
void xmldiff_free (struct xmldiff_tree* diff)
{
	struct xmldiff_tree* cur, *prev;

	if (diff == NULL) {
		return;
	}

	cur = diff->children;
	while (cur != NULL) {
		xmldiff_free(cur);
		prev = cur;
		cur = cur->next;
		free(prev);
	}

	free(diff->path);
}

const char * get_prefix (char * uri, const char * ns_mapping[])
{
	int i;

	for (i=0; ns_mapping[2*i] != NULL; i++) {
		if (strcmp(uri, ns_mapping[2*i+1]) == 0) {
			return(ns_mapping[2*i]);
		}
	}
	return(NULL);
}

/**
 * @brief Add single diff record
 *
 * @param diff	pointer to xmldiff structure
 * @param[in] path	path in XML document where the change occurs
 * @param[in]	op	change type
 *
 * return EXIT_SUCCESS or EXIT_FAILURE
 */
void xmldiff_add_diff (struct xmldiff_tree** diff, const char * ns_mapping[], const char * path, xmlNodePtr node, XMLDIFF_OP op, XML_RELATION rel)
{
	struct xmldiff_tree* new, *cur;
	xmlNodePtr child;
	char* new_path;

	new = malloc(sizeof(struct xmldiff_tree));
	memset(new, 0, sizeof(struct xmldiff_tree));

	/* if added or removed mark all children the same */
	if (op & XMLDIFF_ADD || op & XMLDIFF_REM) {
		for (child = node->children; child != NULL; child = child->next) {
			if (child->type != XML_ELEMENT_NODE) {
				continue;
			}
			asprintf (&new_path, "%s/%s:%s", path, get_prefix((char*)child->ns->href, ns_mapping), child->name);
			xmldiff_add_diff (&new, ns_mapping, new_path, child, op, XML_CHILD);
			free(new_path);
		}
	}

	new->path = strdup(path);
	new->node = node;
	new->op = op;

	if (*diff == NULL) {
		*diff = new;
	} else {
		switch (rel) {
		case XML_PARENT:
			/* (*diff) is our parent */
			(*diff)->parent = new;
			cur = (*diff)->next;
			while (cur != NULL) {
				cur->parent = new;
				cur = cur->next;
			}
			new->children = *diff;
			break;

		case XML_CHILD:
			/* (*diff) is our child */
			if ((*diff)->children == NULL) {
				(*diff)->children = new;
			} else {
				cur = (*diff)->children;
				while (cur->next != NULL) {
					cur = cur->next;
				}
				cur->next = new;
			}
			new->parent = *diff;
			break;

		case XML_SIBLING:
			/* (*diff) is our sibling */
			cur = *diff;
			while (cur->next != NULL) {
				cur = cur->next;
			}
			cur->next = new;
			new->parent = cur->parent;
			break;
		}
	}
}

void xmldiff_addsibling_diff (struct xmldiff_tree** siblings, struct xmldiff_tree** new_sibling) {
	struct xmldiff_tree* last_sibling;

	if (*siblings == NULL) {
		*siblings = *new_sibling;
		return;
	}

	last_sibling = *siblings;
	while (last_sibling->next != NULL) {
		last_sibling = last_sibling->next;
	}

	last_sibling->next = *new_sibling;
	(*new_sibling)->parent = last_sibling->parent;
}

/**
 * @brief Return EXIT_SUCCESS if node1 and node2 have same name and are in the same namespace.
 *
 * This function is used in xmldiff_recursive() for determining that we have
 * found equivalent node in the other document. Only name and namespace is checked
 * because in xmldiff_recursive() is always assured that we are on the same place in
 * both documents.
 *
 * @param node1	One node to compare.
 * @param node2	Other node to compare.
 *
 * @return EXIT_SUCCESS when equivalent EXIT_FAILURE otherwise
 */
int node_cmp(xmlNodePtr node1, xmlNodePtr node2)
{
	int ret = EXIT_FAILURE;
	
	if (node1 != NULL && node2 != NULL) { /* valid nodes */
		if (xmlStrEqual(node1->name, node2->name)) { /*	with same name */
			if (node1->ns == node2->ns) {/* namespace is identical (single object referenced by both nodes) on both NULL */
				ret = EXIT_SUCCESS;
			} else if ((node1->ns == NULL || node2->ns == NULL) || (node1->ns->href == NULL || node2->ns->href == NULL))  { /* one of nodes has no namespace */
				ret = EXIT_FAILURE;
			} else if (xmlStrEqual(node1->ns->href, node2->ns->href)) {
				ret = EXIT_SUCCESS;
			}
		}
	}

	return(ret);
}

/*
 * @brief Return EXIT_SUCCESS if node1 and node2 have same name, are in the same namespace and
 * have the same key values.
 *
 * @param node1	One node to compare.
 * @param node2	Other node to compare.
 *
 * @return EXIT_SUCCESS when equivalent EXIT_FAILURE otherwise
 */
int list_node_cmp(xmlNodePtr node1, xmlNodePtr node2, struct model_tree * model)
{
	int i, ret = EXIT_FAILURE;
	xmlNodePtr node_tmp;
	xmlChar * tmp_str, *node1_keys, *node2_keys;

	if (node_cmp(node1, node2) == EXIT_SUCCESS) {
		/* For every old node create string holding the concatenated key values */
		node1_keys = BAD_CAST strdup ("");
		node2_keys = BAD_CAST strdup ("");
		for (i=0; i<model->keys_count; i++) { /* For every specified key */
			node_tmp = node1->children;
			while (node_tmp) {
				if (xmlStrEqual(node_tmp->name, BAD_CAST model->keys[i])) { /* Find matching leaf in old document */
					tmp_str = xmlNodeGetContent (node_tmp);
					node1_keys  = realloc (node1_keys, sizeof(char) * (strlen((const char*)node1_keys)+strlen((const char*)tmp_str)+1));
					strcat ((char*)node1_keys, (char*)tmp_str); /* Concatenate key value */
					xmlFree (tmp_str);
					break;
				}
				node_tmp = node_tmp->next;
			}
			node_tmp = node2->children;
			while (node_tmp) {
				if (xmlStrEqual(node_tmp->name, BAD_CAST model->keys[i])) { /* Find matching leaf in old document */
					tmp_str = xmlNodeGetContent (node_tmp);
					node2_keys = realloc (node2_keys, sizeof(char) * (strlen((const char*)node2_keys)+strlen((const char*)tmp_str)+1));
					strcat ((char*)node2_keys, (char*)tmp_str); /* Concatenate key value */
					xmlFree (tmp_str);
					break;
				}
				node_tmp = node_tmp->next;
			}
		}
		if (xmlStrEqual(node1_keys, node2_keys)) {
			ret = EXIT_SUCCESS;
		}
		xmlFree(node1_keys);
		xmlFree(node2_keys);
	}

	return(ret);
}


#if 0
XMLDIFF_OP xmldiff_list
{
	/* TODO: Move code for list processing from xmldiff recursive here*/
}

XMLDIFF_OP xmldiff_leaflist ()
{
	/* TODO: Move code for leaf-list processing from xmldiff recursive here*/
}
#endif

/**
 * @brief Recursively go through documents and search for differences. Build
 *		a difference tree starting with leaves.
 *
 * @param diff	returned difference tree, should be NULL when first passed
 * @param ns_mapping	namespace mapings
 * @param path	path of the model node passed
 * @param old_doc	document with the old configuration
 * @param old_node	current node (or sibling) in the old configuration
 * @param new_doc	document with the new configuration
 * @param new_node	current node (or sibling) in the new configuration
 * @param model	current node in the model
 */
XMLDIFF_OP xmldiff_recursive (struct xmldiff_tree** diff, const char *ns_mapping[], char * path, xmlDocPtr old_doc, xmlNodePtr old_node, xmlDocPtr new_doc, xmlNodePtr new_node, struct model_tree * model)
{
	char * next_path, *list_name;
	int i;
	xmlNodePtr old_tmp, new_tmp;
	xmlNodePtr list_old_tmp, list_new_tmp, list_old_inter, list_new_inter;
	XMLDIFF_OP tmp_op, ret_op = XMLDIFF_NONE, item_ret_op;
	xmlChar * old_content, * new_content;
	xmlChar * old_str, *new_str;
	xmlChar * old_keys, *new_keys;
	xmlBufferPtr buf;
	struct xmldiff_tree** tmp_diff;
	xmlNodePtr * list_added, * realloc_tmp;
	int list_added_cnt;

	/* Some of the required documents are missing */
	if (old_doc == NULL || new_doc == NULL || model == NULL) {
		return XMLDIFF_ERR;
	}

	/* On YIN_TYPE_CHOICE the choice element itself is found in neither old config nor new config, but should be further parsed */
	if (model->type == YIN_TYPE_CHOICE) {
		goto model_type;
	}

	/* Find the node from the model in the old configuration */
	old_tmp = old_node;
	while (old_tmp) {
		if (xmlStrEqual (old_tmp->name, BAD_CAST model->name)) {
			break;
		}
		old_tmp = old_tmp->next;
	}

	/* Find the node from the model in the new configuration */
	new_tmp = new_node;
	while (new_tmp) {
		if (xmlStrEqual (new_tmp->name, BAD_CAST model->name)) {
			break;
		}
		new_tmp = new_tmp->next;
	}

	if (old_tmp == NULL && new_tmp == NULL) { /* Node is not in the new config nor the old config */
		return XMLDIFF_NONE;

	} else if (old_tmp == NULL) { /* Node was added */
		xmldiff_add_diff(diff, ns_mapping, path, new_tmp, XMLDIFF_ADD, XML_CHILD);
		return XMLDIFF_ADD;

	} else if (new_tmp == NULL) { /* Node was removed */
		xmldiff_add_diff(diff, ns_mapping, path, old_tmp, XMLDIFF_REM, XML_CHILD);
		return XMLDIFF_REM;

	} else { /* Node is in both configurations, check for internal changes */
model_type:
		switch (model->type) {
		case YIN_TYPE_CONTAINER:
			/* Container */
			ret_op = XMLDIFF_NONE;
			tmp_diff = malloc(sizeof(struct xmldiff_tree*));
			*tmp_diff = NULL;
			tmp_op = XMLDIFF_NONE;
			for (i=0; i<model->children_count; i++) {
				asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
				tmp_op = xmldiff_recursive (tmp_diff, ns_mapping, next_path, old_doc, old_tmp->children, new_doc, new_tmp->children, &model->children[i]);
				free (next_path);
	
				if (tmp_op == XMLDIFF_ERR) {
					return XMLDIFF_ERR;
				} else if (tmp_op != XMLDIFF_NONE) {
					ret_op = XMLDIFF_CHAIN;
				}
			}
			if (tmp_op != XMLDIFF_NONE) {
				xmldiff_add_diff (tmp_diff, ns_mapping, path, new_tmp, tmp_op, XML_PARENT);
				*tmp_diff = (*tmp_diff)->parent;
				xmldiff_addsibling_diff (diff, tmp_diff);
			}
			free(tmp_diff);
			break;
		case YIN_TYPE_CHOICE: 
			/* Choice */ 
			ret_op = XMLDIFF_NONE;
			/* Trim the choice path, replace it with the children directly */
			*strrchr(path, '/') = '\0';

			for (i=0; i<model->children_count; i++) {
				asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
				/* We are moving down the model only (not in the configuration) */
				tmp_op = xmldiff_recursive (diff, ns_mapping, next_path, old_doc, old_node, new_doc, new_node, &model->children[i]);
				free (next_path);

				/* Assuming there is only one child of this choice (as it should be), we return this child's operation, the choice itself is de-facto skipped */
				if (tmp_op != XMLDIFF_NONE) {
					ret_op = tmp_op;
					break;
				}
			}

			break;
		case YIN_TYPE_LEAF: 
		 	/* Leaf */
			old_content = xmlNodeGetContent (old_tmp);
			new_content = xmlNodeGetContent (new_tmp);
			if (xmlStrEqual (old_content, new_content)) {
				ret_op = XMLDIFF_NONE;
			} else {
				ret_op = XMLDIFF_MOD;
				xmldiff_add_diff (diff, ns_mapping, path, new_tmp, XMLDIFF_MOD, XML_SIBLING);
			}
			xmlFree (old_content);
			xmlFree (new_content);
			break;
		case YIN_TYPE_LIST:
			/* List */
			ret_op = XMLDIFF_NONE;
			list_added = NULL;
			list_added_cnt = 0;

			/* Find matches according to the key elements, process all the elements inside recursively */
			/* Not matching are _ADD or _REM */
			/* Maching are _NONE or _CHAIN, according to the return values of the recursive calls */

			/* Go through the old nodes and search for matching nodes in the new document*/
			list_old_tmp = old_tmp;
			while (list_old_tmp) {
				/* We have to make sure that this really is a list node we are checking now */
				if (node_cmp(old_tmp, list_old_tmp)) {
					list_old_tmp = list_old_tmp->next;
					continue;
				}

				item_ret_op = XMLDIFF_NONE;
				/* For every old node create string holding the concatenated key values */
				old_keys = BAD_CAST strdup ("");
				for (i=0; i<model->keys_count; i++) { /* For every specified key */
					list_old_inter = list_old_tmp->children;
					while (list_old_inter) {
						if (xmlStrEqual(list_old_inter->name, BAD_CAST model->keys[i])) { /* Find matching leaf in old document */
							old_str = xmlNodeGetContent (list_old_inter);
							old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)old_str)+1));
							strcat ((char*)old_keys, (char*)old_str); /* Concatenate key value */
							xmlFree (old_str);
							break;
						}
						list_old_inter = list_old_inter->next;
					}
				}

				/* Go through the new list */
				list_new_tmp = new_tmp;
				while (list_new_tmp) {
					if (node_cmp(old_tmp, list_new_tmp)) {
						list_new_tmp = list_new_tmp->next;
						continue;
					}

					new_keys = BAD_CAST strdup ("");
					for (i=0; i<model->keys_count; i++) {
						list_new_inter = list_new_tmp->children;
						while (list_new_inter) {
							if (xmlStrEqual(list_new_inter->name, BAD_CAST model->keys[i])) {
								new_str = xmlNodeGetContent (list_new_inter);
								new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)new_str)+1));
								strcat ((char*)new_keys, (char*)new_str);
								xmlFree (new_str);
								break;
							}
							list_new_inter = list_new_inter->next;
						}
					}
					if (strcmp ((const char*)old_keys, (const char*)new_keys) == 0) { /* Matching item found */
						free (new_keys);
						break;
					}
					free (new_keys);
					list_new_tmp = list_new_tmp->next;
				}
				free (old_keys);

				if (list_new_tmp == NULL) { /* Item NOT found in the new document -> removed */
					xmldiff_add_diff (diff, ns_mapping, path, list_old_tmp, XMLDIFF_REM, XML_SIBLING);
					ret_op = XMLDIFF_CHAIN;
				} else { /* Item found -> check for changes recursively*/
					tmp_diff = malloc(sizeof(struct xmldiff_tree*));
					*tmp_diff = NULL;
					for (i=0; i<model->children_count; i++) {
						asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
						tmp_op = xmldiff_recursive (tmp_diff, ns_mapping, next_path, old_doc, list_old_tmp->children, new_doc, list_new_tmp->children, &model->children[i]);
						free (next_path);

						if (tmp_op == XMLDIFF_ERR) {
							return XMLDIFF_ERR;
						} else {
							item_ret_op |= tmp_op;
						}
					}

					if (item_ret_op != XMLDIFF_NONE) {
						/* There actually was a change, so we append those changes as our children and add our change as a sibling */
						xmldiff_add_diff (tmp_diff, ns_mapping, path, list_new_tmp, XMLDIFF_CHAIN, XML_PARENT);
						*tmp_diff = (*tmp_diff)->parent;
						xmldiff_addsibling_diff (diff, tmp_diff);
						ret_op = XMLDIFF_CHAIN;
					} else {
						free(tmp_diff);
					}
				}
				list_old_tmp = list_old_tmp->next;
			}

			/* Go through the new nodes and search for matching nodes in the old document*/
			list_new_tmp = new_tmp;
			while (list_new_tmp) {
				if (node_cmp(new_tmp, list_new_tmp)) {
					list_new_tmp = list_new_tmp->next;
					continue;
				}

				item_ret_op = XMLDIFF_NONE;
				/* For every new node create string holding the concatenated key values */
				new_keys = BAD_CAST strdup ("");
				for (i=0; i<model->keys_count; i++) { /* For every specified key */
					list_new_inter = list_new_tmp->children;
					while (list_new_inter) {
						if (xmlStrEqual(list_new_inter->name, BAD_CAST model->keys[i])) { /* Find matching leaf in the old document */
							new_str = xmlNodeGetContent (list_new_inter);
							new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)new_str)+1));
							strcat ((char*)new_keys, (char*)new_str); /* Concatenate key value */
							xmlFree (new_str);
							break;
						}
						list_new_inter = list_new_inter->next;
					}
				}
				/* Go through the new list */
				list_old_tmp = old_tmp;
				while (list_old_tmp) {
					if (node_cmp(new_tmp, list_old_tmp)) {
						list_old_tmp = list_old_tmp->next;
						continue;
					}

					old_keys = BAD_CAST strdup ("");
					for (i=0; i<model->keys_count; i++) {
						list_old_inter = list_old_tmp->children;
						while (list_old_inter) {
							if (xmlStrEqual(list_old_inter->name, BAD_CAST model->keys[i])) {
								old_str = xmlNodeGetContent (list_old_inter);
								old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)old_str)+1));
								strcat ((char*)old_keys, (char*)old_str);
								xmlFree (old_str);
								break;
							}
							list_old_inter = list_old_inter->next;
						}
					}
					if (strcmp ((const char*)old_keys, (const char*)new_keys) == 0) { /* Matching item found */
						free (old_keys);
						break;
					}
					free (old_keys);
					list_old_tmp = list_old_tmp->next;
				}
				free (new_keys);
				
				if (list_old_tmp == NULL) { /* Item NOT found in the old document -> added */
					xmldiff_add_diff (diff, ns_mapping, path, list_new_tmp, XMLDIFF_ADD, XML_SIBLING);
					ret_op = XMLDIFF_CHAIN;
					/* remeber that the node was added*/
					if ((realloc_tmp = realloc (list_added, ++list_added_cnt * sizeof(xmlNodePtr))) == NULL) {
						free(list_added);
						return (XMLDIFF_ERR);
					} else {
						list_added = realloc_tmp;
						list_added[list_added_cnt-1] = list_new_tmp;
					}
				} else {
					/* We already checked for changes in these nodes */
				}
				list_new_tmp = list_new_tmp->next;
			}
			/* list is ordered by user */
			if (model->ordering == YIN_ORDER_USER) {
				/* No node was added or removed */
				if (ret_op == XMLDIFF_NONE) {
					/* go through old and new list and compare pairs */
					/* if there is a difference the list was somehow reordered */
					list_old_tmp = old_tmp;
					list_new_tmp = new_tmp;

					while (list_old_tmp && list_new_tmp) {
						if (!xmlStrEqual(list_old_tmp->name, BAD_CAST model->name)) {
							list_old_tmp = list_old_tmp->next;
							continue;
						}
						if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
							list_new_tmp = list_new_tmp->next;
							continue;
						}

						if (!xmlStrEqual(list_old_tmp->name, list_new_tmp->name)) {
							/* difference found */
							ret_op = XMLDIFF_REORDER;
							break;
						}

						if (list_node_cmp(list_old_tmp, list_new_tmp, model)) {
							ret_op = XMLDIFF_REORDER;
							break;
						}

						list_old_tmp = list_old_tmp->next;
						list_new_tmp = list_new_tmp->next;
					}

					if (ret_op == XMLDIFF_REORDER) {
						/* inform all siblings that order changed (XMLDIFF_SIBLING) */
						list_new_tmp = new_tmp;
						while (list_new_tmp) {
							if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
								list_new_tmp = list_new_tmp->next;
								continue;
							}

							xmldiff_add_diff(diff, ns_mapping, path, list_new_tmp, XMLDIFF_SIBLING, XML_SIBLING);
							list_new_tmp = list_new_tmp->next;
						}
					}
				} else if (ret_op == XMLDIFF_CHAIN) {
					list_new_tmp = new_tmp;
					while (list_new_tmp) {
						if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
							list_new_tmp = list_new_tmp->next;
							continue;
						}
						for (i=0; i<list_added_cnt; i++) {
							if (list_added[i] == list_new_tmp) {
								break;
							}
						}

						if (i == list_added_cnt) { /* no match in list_added */
							xmldiff_add_diff(diff, ns_mapping, path, list_new_tmp, XMLDIFF_SIBLING, XML_SIBLING);
						}
						list_new_tmp = list_new_tmp->next;
					}
				}
			}
			free(list_added);
			break;
		case YIN_TYPE_LEAFLIST:
			/* Leaf-list */
			ret_op = XMLDIFF_NONE;
			list_name = strrchr(path, ':')+1;
			list_added = NULL;
			list_added_cnt = 0;

			/* Search for matches, only _ADD and _REM will be here */
			/* For each in the old node find one from the new nodes or log as _REM */
			list_old_tmp = old_tmp;
			while (list_old_tmp) {
				if (!xmlStrEqual (BAD_CAST list_name, list_old_tmp->name)) {
					list_old_tmp = list_old_tmp->next;
					continue;
				}
				old_str = xmlNodeGetContent(list_old_tmp);
				list_new_tmp = new_tmp;
				while (list_new_tmp) {
					if (!xmlStrEqual (BAD_CAST list_name, list_new_tmp->name)) {
						list_new_tmp = list_new_tmp->next;
						continue;
					}
					new_str = xmlNodeGetContent (list_new_tmp);
					if (xmlStrEqual (old_str, new_str)) {
						xmlFree (new_str);
						/* Equivalent found */
						break;
					}
					xmlFree (new_str);
					list_new_tmp = list_new_tmp->next;
				}
				xmlFree (old_str);
				if (list_new_tmp == NULL) {
					xmldiff_add_diff (diff, ns_mapping, path, list_old_tmp, XMLDIFF_REM, XML_SIBLING);
					ret_op = XMLDIFF_CHAIN;
				}
				list_old_tmp = list_old_tmp->next;
			}
			/* For each in the new node find one from the old nodes or log as _ADD */
			list_new_tmp = new_tmp;
			while (list_new_tmp) {
				if (!xmlStrEqual (BAD_CAST list_name, list_new_tmp->name)) {
					list_new_tmp = list_new_tmp->next;
					continue;
				}
				new_str = xmlNodeGetContent(list_new_tmp);
				list_old_tmp = old_tmp;
				while (list_old_tmp) {
					if (!xmlStrEqual (BAD_CAST list_name, list_old_tmp->name)) {
						list_old_tmp = list_old_tmp->next;
						continue;
					}
					old_str = xmlNodeGetContent (list_old_tmp);
					if (xmlStrEqual (old_str, new_str)) {
						xmlFree (old_str);
						/* Equivalent found */
						break;
					}
					xmlFree (old_str);
					list_old_tmp = list_old_tmp->next;
				}
				xmlFree (new_str);
				if (list_old_tmp == NULL) {
					xmldiff_add_diff (diff, ns_mapping, path, list_new_tmp, XMLDIFF_ADD, XML_SIBLING);
					ret_op = XMLDIFF_CHAIN;
					/* remeber that the node was added*/
					if ((realloc_tmp = realloc (list_added, ++list_added_cnt * sizeof(xmlNodePtr))) == NULL) {
						free(list_added);
						return (XMLDIFF_ERR);
					} else {
						list_added = realloc_tmp;
						list_added[list_added_cnt-1] = list_new_tmp;
					}
				}
				list_new_tmp = list_new_tmp->next;
			}
			/* leaf-list is ordered by user */
			if (model->ordering == YIN_ORDER_USER) {
				/* No node was added or removed */
				if (ret_op == XMLDIFF_NONE) {
					/* go through old and new list and compare pairs */
					/* if there is a difference the list was somehow reordered */
					list_old_tmp = old_tmp;
					list_new_tmp = new_tmp;

					while (list_old_tmp && list_new_tmp) {
						if (!xmlStrEqual(list_old_tmp->name, BAD_CAST model->name)) {
							list_old_tmp = list_old_tmp->next;
							continue;
						}
						if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
							list_new_tmp = list_new_tmp->next;
							continue;
						}

						if (!xmlStrEqual(list_old_tmp->name, list_new_tmp->name)) {
							/* difference found */
							ret_op = XMLDIFF_REORDER;
							break;
						}

						if (!xmlStrEqual(list_old_tmp->children->content, list_new_tmp->children->content)) {
							ret_op = XMLDIFF_REORDER;
							break;
						}

						list_old_tmp = list_old_tmp->next;
						list_new_tmp = list_new_tmp->next;
					}

					if (ret_op == XMLDIFF_REORDER) {
						/* inform all siblings that order changed (XMLDIFF_SIBLING) */
						list_new_tmp = new_tmp;
						while (list_new_tmp) {
							if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
								list_new_tmp = list_new_tmp->next;
								continue;
							}

							xmldiff_add_diff(diff, ns_mapping, path, list_new_tmp, XMLDIFF_SIBLING, XML_SIBLING);
							list_new_tmp = list_new_tmp->next;
						}
					}
				} else if (ret_op == XMLDIFF_CHAIN) {
					list_new_tmp = new_tmp;
					while (list_new_tmp) {
						if (!xmlStrEqual(list_new_tmp->name, BAD_CAST model->name)) {
							list_new_tmp = list_new_tmp->next;
							continue;
						}
						for (i=0; i<list_added_cnt; i++) {
							if (list_added[i] == list_new_tmp) {
								break;
							}
						}

						if (i == list_added_cnt) { /* no match in list_added */
							xmldiff_add_diff(diff, ns_mapping, path, list_new_tmp, XMLDIFF_SIBLING, XML_SIBLING);
						}
						list_new_tmp = list_new_tmp->next;
					}
				}
			}
			free(list_added);
			break;
		case YIN_TYPE_ANYXML:
			/* Anyxml */
			/* Serialize and compare strings */
			/* TODO: find better solution in future */
			buf = xmlBufferCreate ();
			xmlNodeDump (buf, old_doc, old_tmp, 0, 0);
			old_str = xmlStrdup(xmlBufferContent(buf));
			xmlBufferEmpty (buf);
			xmlNodeDump (buf, new_doc, new_tmp, 0, 0);
			new_str = xmlStrdup(xmlBufferContent(buf));
			xmlBufferFree (buf);

			if (xmlStrEqual(old_str, new_str)) {
				ret_op = XMLDIFF_NONE;
			} else {
				xmldiff_add_diff (diff, ns_mapping, path, new_tmp, XMLDIFF_MOD, XML_SIBLING);
				ret_op = XMLDIFF_CHAIN;
			}
			xmlFree(old_str);
			xmlFree(new_str);
			break;
		default:
			/* No other type is supported now */
			break;
		}
	}

	return ret_op;
}

/**
 * @brief Module top level function
 *
 * @param old		old version of XML document
 * @param new		new version of XML document
 * @param model	data model in YANG format
 *
 * @return xmldiff structure holding all differences between XML documents or NULL
 */
XMLDIFF_OP xmldiff_diff (struct xmldiff_tree** diff, xmlDocPtr old, xmlDocPtr new, struct model_tree * model, const char * ns_mapping[])
{
	char* path;
	XMLDIFF_OP ret_op;

	if (old == NULL || new == NULL || diff == NULL) {
		return XMLDIFF_ERR;
	}

	asprintf (&path, "/%s:%s", model->children->ns_prefix, model->children->name);
	ret_op = xmldiff_recursive (diff, ns_mapping, path, old, old->children, new, new->children, &model->children[0]);
	free (path);

	return ret_op;
}
