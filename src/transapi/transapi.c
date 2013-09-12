#include <string.h>
#include <stdio.h>

#include "transapi_internal.h"
#include "xmldiff.h"
#include "../netconf_internal.h"
#include "../datastore/edit_config.h"

struct transapi_callbacks_info {
	xmlDocPtr old;
	xmlDocPtr new;
	xmlDocPtr model;
	keyList keys;
	int libxml2;
	/*
	 * according to libxml2 value one of struct transapi_xml_data_callbacks or
	 * struct transapi_data_callbacks
	 */
	void *calls;
};

static int transapi_revert_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt)
{
	struct xmldiff_tree *child;
	xmlNodePtr parent, xmlnode;
	struct transapi_xml_data_callbacks *xmlcalls = NULL;
	struct transapi_data_callbacks *stdcalls = NULL;
	xmlBufferPtr buf;
	char* node;
	int ret;
	XMLDIFF_OP op;

	for(child = tree->children; child != NULL; child = child->next) {
		transapi_revert_callbacks_recursive(info, child, erropt);
		if (tree->callback && !tree->applied &&
				(erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP || erropt == NC_EDIT_ERROPT_CONT)) {
			/* discard proposed changes */
			if (tree->op == XMLDIFF_ADD && tree->node != NULL ) {
				/* remove element to add from the new XML tree */
				xmlUnlinkNode(tree->node);
				xmlFreeNode(tree->node);
			} else if (tree->op == XMLDIFF_REM && tree->node != NULL ) {
				/* reconnect old node supposed to be romeved back to the new XML tree */
				parent = find_element_equiv(info->new, tree->node->parent, info->model, info->keys);
				xmlAddChild(parent, xmlCopyNode(tree->node, 1));
			} else if ((tree->op == XMLDIFF_MOD || tree->op == XMLDIFF_CHAIN) && tree->node != NULL ) {
				/* replace new node with the previous one */
				parent = find_element_equiv(info->old, tree->node->parent, info->model, info->keys);
				for (xmlnode = parent->children; xmlnode != NULL; xmlnode = xmlnode->next) {
					if (matching_elements(tree->node, xmlnode, info->keys, 0)) {
						break;
					}
				}
				if (xmlnode != NULL ) {
					/* replace subtree */
					xmlReplaceNode(tree->node, xmlnode);
					xmlFreeNode(tree->node);
				} else {
					WARN("Unable to discard not executed changes from XML tree: previous subtree version not found.")
				}
			}
		}

		if (erropt == NC_EDIT_ERROPT_ROLLBACK && tree->callback && tree->applied) {
			/*
			 * do not affect XML tree, just use transAPI callbacks
			 * to revert applied chenges. XML tree is reverted in
			 * nc_rpc_apply()
			 */
			if (tree->op == XMLDIFF_ADD && tree->node != NULL ) {
				/* node was added, now remove it */
				op = XMLDIFF_REM;
				xmlnode = tree->node;
			} else if (tree->op == XMLDIFF_REM && tree->node != NULL ) {
				/* node was removed, add it back */
				op = XMLDIFF_ADD;
				xmlnode = tree->node;
			} else if ((tree->op == XMLDIFF_MOD || tree->op == XMLDIFF_CHAIN) && tree->node != NULL ) {
				/* node was modified, replace it with previous version */
				parent = find_element_equiv(info->old, tree->node->parent, info->model, info->keys);
				for (xmlnode = parent->children; xmlnode != NULL; xmlnode = xmlnode->next) {
					if (matching_elements(tree->node, xmlnode, info->keys, 0)) {
						break;
					}
				}
				if (xmlnode != NULL ) {
					op = tree->op;
					/* xmlnode already set */
				} else {
					ERROR("Unable to revert executed changes: previous subtree version not found.");
					/* continue with a next change */
					continue;
				}
			}

			/* revert changes */
			if (info->libxml2) {
				xmlcalls = (struct transapi_xml_data_callbacks*)(info->calls);
				ret = xmlcalls->callbacks[tree->priority - 1].func(op, xmlnode, &xmlcalls->data);
			} else {
				/* if node was removed, it was copied from old XML doc, else from new XML doc */
				stdcalls = (struct transapi_data_callbacks*)(info->calls);
				buf = xmlBufferCreate();
				xmlNodeDump(buf, xmlnode->doc, xmlnode, 1, 0);
				node = (char*) xmlBufferContent(buf);
				ret = stdcalls->callbacks[tree->priority - 1].func(op, node, &stdcalls->data);
				xmlBufferFree(buf);
			}

			if (ret != EXIT_SUCCESS) {
				WARN("Reverting configuration changes via transAPI failed, configuration may be inconsistent.");
			}
		}
	}

	return (EXIT_SUCCESS);
}

/* call the callbacks in the order set by the priority of each change */
static int transapi_apply_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt) {
	struct xmldiff_tree* child, *cur_min;
	int ret, retval = EXIT_SUCCESS;
	xmlBufferPtr buf;
	char* node;
	struct transapi_xml_data_callbacks *xmlcalls = NULL;
	struct transapi_data_callbacks *stdcalls = NULL;

	do {
		cur_min = NULL;
		child = tree->children;
		while (child != NULL) {
			if (child->callback && !child->applied) {
				/* Valid change with a callback */
				if (cur_min == NULL || cur_min->priority > child->priority) {
					cur_min = child;
				}
			}
			child = child->next;
		}

		if (cur_min != NULL) {
			/* Process this child recursively */
			if (transapi_apply_callbacks_recursive(info, cur_min, erropt) != EXIT_SUCCESS) {
				if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP || erropt == NC_EDIT_ERROPT_ROLLBACK) {
					return (EXIT_FAILURE);
				} /* on continue-on-error, just remember return value, but continue */
				retval = EXIT_FAILURE;
			}
		}
	} while (cur_min != NULL);

	/* Finally call our callback */
	if (tree->callback) {
		DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
		if (info->libxml2) {
			xmlcalls = (struct transapi_xml_data_callbacks*)(info->calls);
			ret = xmlcalls->callbacks[tree->priority-1].func(tree->op, tree->node, &xmlcalls->data);
		} else {
			/* if node was removed, it was copied from old XML doc, else from new XML doc */
			stdcalls = (struct transapi_data_callbacks*)(info->calls);
			buf = xmlBufferCreate();
			xmlNodeDump(buf, tree->op == XMLDIFF_REM ? info->old : info->new, tree->node, 1, 0);
			node = (char*)xmlBufferContent(buf);
			ret = stdcalls->callbacks[tree->priority-1].func(tree->op, node, &stdcalls->data);
			xmlBufferFree(buf);
		}
		if (ret != EXIT_SUCCESS) {
			ERROR("Callback for path %s failed (%d).", tree->path, ret);
			return (EXIT_FAILURE);
		} else {
			tree->applied = true;
		}
	}

	return (retval);
}

/* will be called by library after change in running datastore */
int transapi_running_changed (void* c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct data_model *model, NC_EDIT_ERROPT_TYPE erropt, int libxml2)
{
	struct xmldiff_tree* diff = NULL;
	struct transapi_callbacks_info info;
	
	if (xmldiff_diff(&diff, old_doc, new_doc, model->model_tree, ns_mapping) == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Failed to create the tree of differences.");
		xmldiff_free(diff);
		return EXIT_FAILURE;
	} else if (diff != NULL) {
		if (xmldiff_set_priorities(diff, c) != EXIT_SUCCESS) {
			VERB("There was not found a single callback for the configuration change.");
		} else {
			info.old = old_doc;
			info.new = new_doc;
			info.model = model->xml;
			info.keys = get_keynode_list(info.model);
			info.libxml2 = libxml2;
			info.calls = c;

			if (transapi_apply_callbacks_recursive(&info, diff, erropt) != EXIT_SUCCESS) {
				/* revert not applied changes from XML tree */
				transapi_revert_callbacks_recursive(&info, diff, erropt);

				keyListFree(info.keys);
				xmldiff_free(diff);
				free(diff);
				return EXIT_FAILURE;
			}
			keyListFree(info.keys);
		}
	} else {
		VERB("Nothing changed.");
	}

	xmldiff_free(diff);
	free(diff);
	return EXIT_SUCCESS;
}
