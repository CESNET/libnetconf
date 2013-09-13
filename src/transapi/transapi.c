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

static void transapi_revert_xml_tree(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree)
{
	xmlNodePtr parent, xmlnode;
	struct xmldiff_tree* child;

	DBG("Transapi revert XML tree (%s, proposed operation %d).", tree->path, tree->op);
	/* discard proposed changes */
	if ((tree->op & XMLDIFF_ADD) && tree->node != NULL ) {
		/* remove element to add from the new XML tree */
		xmlUnlinkNode(tree->node);
		xmlFreeNode(tree->node);
		tree->node = NULL;
	} else if ((tree->op & XMLDIFF_REM) && tree->node != NULL ) {
		/* reconnect old node supposed to be romeved back to the new XML tree */
		parent = find_element_equiv(info->new, tree->node->parent, info->model, info->keys);
		xmlAddChild(parent, xmlCopyNode(tree->node, 1));
	} else if ((tree->op & XMLDIFF_MOD) && tree->node != NULL ) {
		/* replace new node with the previous one */
		for (child = tree->children; child != NULL; child = child->next) {
			if (child->priority != 0) {
				/* this node is already solved by previous recursion */
				continue;
			}

			/*
			 * we have node with priority 0, it means it was not solved
			 * yet and we want to replace it by the previous config data
			 */
			parent = find_element_equiv(info->old, tree->node->parent, info->model, info->keys);
			for (xmlnode = parent->children; xmlnode != NULL; xmlnode = xmlnode->next) {
				if (matching_elements(tree->node, xmlnode, info->keys, 0)) {
					break;
				}
			}
			if (xmlnode != NULL ) {
				/* replace subtree */
				xmlReplaceNode(child->node, xmlCopyNode(xmlnode, 1));
				xmlFreeNode(child->node);
				child->node = NULL;
			} else {
				WARN("Unable to discard not executed changes from XML tree: previous subtree version not found (path %s).", child->path);
			}
		}
	} /* else XMLDIFF_CHAIN is not interesting here (stop-on-error) */
	tree->applied = true;
}

static int transapi_revert_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err** error)
{
	struct xmldiff_tree *child;
	xmlNodePtr parent, xmlnode;
	struct transapi_xml_data_callbacks *xmlcalls = NULL;
	struct transapi_data_callbacks *stdcalls = NULL;
	xmlBufferPtr buf;
	char* node;
	int ret;
	XMLDIFF_OP op;
	struct nc_err *new_error = NULL;

	/*
	 * On Rollback-on-error, we have to revert applied changes, but we can
	 * tracing the tree when the node priority is 0. It means that the node
	 * neither its children do not contain any callback.
	 * In case of other operations, we can stop the tree tracing based on
	 * applied value - if the node changes were applied, because
	 * we are not calling the callbacks (we are not reverting applied changes),
	 * but we are reverting only changes made to the XML tree but not applied
	 * via transAPI.
	 */

	if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
		/* do the recursion - process children */
		for(child = tree->children; child != NULL; child = child->next) {
			/*
			 * current node either all its children were applied and we
			 * don't need to revert xorresponding XML tree changes
			 */
			if (child->applied) {
				continue;
			}

			/* do the recursion */
			transapi_revert_callbacks_recursive(info, child, erropt, error);
		}

		/* process the current node */
		if (tree->priority != 0) {
			/* discard proposed changes */
			transapi_revert_xml_tree(info, tree);
		}

	} else if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
		/* do the recursion - process children */
		for(child = tree->children; child != NULL; child = child->next) {
			/* if priority == 0 then the node neither its children have no callbacks */
			if (child->priority == 0) {
				continue;
			}

			/* do the recursion */
			transapi_revert_callbacks_recursive(info, child, erropt, error);
		}

		/* process the current node */
		/* current node has no callback function or it was not applied */
		if (!tree->callback || !tree->applied) {
			return (EXIT_SUCCESS);
		}

		/*
		 * do not affect XML tree, just use transAPI callbacks
		 * to revert applied chenges. XML tree is reverted in
		 * nc_rpc_apply()
		 */
		if ((tree->op & XMLDIFF_ADD) && tree->node != NULL ) {
			/* node was added, now remove it */
			op = XMLDIFF_REM;
			xmlnode = tree->node;
		} else if ((tree->op & XMLDIFF_REM) && tree->node != NULL ) {
			/* node was removed, add it back */
			op = XMLDIFF_ADD;
			xmlnode = tree->node;
		} else if (((tree->op & XMLDIFF_MOD) || (tree->op & XMLDIFF_CHAIN)) && tree->node != NULL ) {
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
				return (EXIT_FAILURE);
			}
		}

		DBG("Transapi revert callback %s with op %d.", tree->path, op);

		/* revert changes */
		if (info->libxml2) {
			xmlcalls = (struct transapi_xml_data_callbacks*) (info->calls);
			ret = xmlcalls->callbacks[tree->priority - 1].func(&xmlcalls->data, op, xmlnode, &new_error);
		} else {
			/* if node was removed, it was copied from old XML doc, else from new XML doc */
			stdcalls = (struct transapi_data_callbacks*) (info->calls);
			buf = xmlBufferCreate();
			xmlNodeDump(buf, xmlnode->doc, xmlnode, 1, 0);
			node = (char*) xmlBufferContent(buf);
			ret = stdcalls->callbacks[tree->priority - 1].func(&stdcalls->data, op, node, &new_error);
			xmlBufferFree(buf);
		}

		if (ret != EXIT_SUCCESS) {
			WARN("Reverting configuration changes via transAPI failed, configuration may be inconsistent.");
			if (*error != NULL && new_error != NULL) {
				/* concatenate errors */
				new_error->next = *error;
				*error = new_error;
			} else if (new_error != NULL) {
				*error = new_error;
			}

			return (EXIT_FAILURE);
		}
	}

	return (EXIT_SUCCESS);
}

/* call the callbacks in the order set by the priority of each change */
static int transapi_apply_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error) {
	struct xmldiff_tree* child, *cur_min;
	int ret, retval = EXIT_SUCCESS;
	xmlBufferPtr buf;
	char* node;
	struct transapi_xml_data_callbacks *xmlcalls = NULL;
	struct transapi_data_callbacks *stdcalls = NULL;
	struct nc_err *new_error = NULL;

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
			if (transapi_apply_callbacks_recursive(info, cur_min, erropt, error) != EXIT_SUCCESS) {
				if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP || erropt == NC_EDIT_ERROPT_ROLLBACK) {
					return (EXIT_FAILURE);
				}
				/*
				 * on continue-on-error, continue with processing next
				 * change, but remember that we have failed
				 */
				retval = EXIT_FAILURE;
			}
		}
	} while (cur_min != NULL);

	/* Finally call our callback */
	if (tree->callback) {
		DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
		if (info->libxml2) {
			xmlcalls = (struct transapi_xml_data_callbacks*)(info->calls);
			ret = xmlcalls->callbacks[tree->priority-1].func(&xmlcalls->data, tree->op, tree->node, &new_error);
		} else {
			/* if node was removed, it was copied from old XML doc, else from new XML doc */
			stdcalls = (struct transapi_data_callbacks*)(info->calls);
			buf = xmlBufferCreate();
			xmlNodeDump(buf, tree->op == XMLDIFF_REM ? info->old : info->new, tree->node, 1, 0);
			node = (char*)xmlBufferContent(buf);
			ret = stdcalls->callbacks[tree->priority-1].func(&stdcalls->data, tree->op, node, &new_error);
			xmlBufferFree(buf);
		}
		if (ret != EXIT_SUCCESS) {
			ERROR("Callback for path %s failed (%d).", tree->path, ret);
			if (*error != NULL) {
				/* concatenate errors */
				new_error->next = *error;
				*error = new_error;
			} else if (new_error != NULL) {
				*error = new_error;
			}

			if (erropt == NC_EDIT_ERROPT_CONT) {
				/* on continue-on-error, return not applied changes immediately and then continue */
				transapi_revert_xml_tree(info, tree);
				tree->applied = true;
			}

			return (EXIT_FAILURE);
		}
	}
	/* mark subtree as solved */
	tree->applied = true;

	return (retval);
}

/* will be called by library after change in running datastore */
int transapi_running_changed(void* c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct data_model *model, NC_EDIT_ERROPT_TYPE erropt, int libxml2, struct nc_err **error)
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

			if (transapi_apply_callbacks_recursive(&info, diff, erropt, error) != EXIT_SUCCESS) {
				if (erropt != NC_EDIT_ERROPT_CONT) {
					/* revert not applied changes from XML tree */
					transapi_revert_callbacks_recursive(&info, diff, erropt, error);
				}

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
