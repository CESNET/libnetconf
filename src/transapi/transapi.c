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
};

static int transapi_revert_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt)
{
	struct xmldiff_tree *child;
	xmlNodePtr parent;

	for(child = tree->children; child != NULL; child = child->next) {
		transapi_revert_callbacks_recursive(info, child, erropt);
		if (!tree->applied) {
			if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
				/* discard proposed changes */
				if (tree->op == XMLDIFF_ADD && tree->node != NULL ) {
					/* remove element to add from the new XML tree */
					xmlUnlinkNode(tree->node);
					xmlFreeNode(tree->node);
				} else if (tree->op == XMLDIFF_REM && tree->node != NULL ) {
					/* reconnect old node supposed to be romeved back to the new XML tree */
					parent = find_element_equiv(info->new, tree->node->parent, info->model, info->keys);
					xmlAddChild(parent, xmlCopyNode(tree->node, 1));
				}
			}
		}
	}

	return (EXIT_SUCCESS);
}

/* call the callbacks in the order set by the priority of each change */
static int transapi_apply_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, void* calls, NC_EDIT_ERROPT_TYPE erropt, int libxml2) {
	struct xmldiff_tree* child, *cur_min;
	int ret;
	xmlBufferPtr buf;
	char* node;
	struct transapi_xml_data_callbacks *xmlcalls = NULL;
	struct transapi_data_callbacks *stdcalls = NULL;

	do {
		cur_min = NULL;
		child = tree->children;
		while (child != NULL) {
			if (child->priority && !child->applied) {
				/* Valid change with a callback or callback in child (priority > 0) not yet applied */
				if (cur_min == NULL || cur_min->priority > child->priority) {
					cur_min = child;
				}
			}
			child = child->next;
		}

		if (cur_min != NULL) {
			/* Process this child recursively */
			if (transapi_apply_callbacks_recursive(info, cur_min, calls, erropt, libxml2) != EXIT_SUCCESS) {
				if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
					return EXIT_FAILURE;
				}
			}
		}
	} while (cur_min != NULL);

	/* Finally call our callback */
	if (tree->callback) {
		DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
		if (libxml2) {
			xmlcalls = (struct transapi_xml_data_callbacks*)calls;
			ret = xmlcalls->callbacks[tree->priority-1].func(tree->op, tree->node, &xmlcalls->data);
		} else {
			/* if node was removed, it was copied from old XML doc, else from new XML doc */
			stdcalls = (struct transapi_data_callbacks*)calls;
			buf = xmlBufferCreate();
			xmlNodeDump(buf, tree->op == XMLDIFF_REM ? info->old : info->new, tree->node, 1, 0);
			node = (char*)xmlBufferContent(buf);
			ret = stdcalls->callbacks[tree->priority-1].func(tree->op, node, &stdcalls->data);
			xmlBufferFree(buf);
		}
		if (ret != EXIT_SUCCESS) {
			ERROR("Callback for path %s failed (%d).", tree->path, ret);

			if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
				/* not yet performed changes will be discarded */
				return EXIT_FAILURE;
			} else if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
			} else if (erropt == NC_EDIT_ERROPT_CONT) {
			}
		}
	}
	/* mark subtree as solved */
	tree->applied = true;

	return EXIT_SUCCESS;
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

			if (transapi_apply_callbacks_recursive(&info, diff, c, erropt, libxml2) != EXIT_SUCCESS) {
				/* revert not applied changes from XML tree */
				transapi_revert_callbacks_recursive(&info, diff, erropt);

				xmldiff_free(diff);
				free(diff);
				return EXIT_FAILURE;
			}
		}
	} else {
		VERB("Nothing changed.");
	}

	xmldiff_free(diff);
	free(diff);
	return EXIT_SUCCESS;
}
