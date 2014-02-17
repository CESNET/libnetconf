#include <string.h>
#include <stdio.h>

#include "transapi_internal.h"
#include "xmldiff.h"
#include "../netconf_internal.h"
#include "../datastore/edit_config.h"

#define REVERT_CALLBACK_SUCCESS		0
#define REVERT_CALLBACK_ERROR		1
#define REVERT_CALLBACK_CONTINUE	2

#define APPLY_CALLBACK_SUCCESS		0
#define APPLY_CALLBACK_ERROR		1
#define APPLY_CALLBACK_CONTINUE		2

struct transapi_callbacks_info {
	xmlDocPtr old;
	xmlDocPtr new;
	xmlDocPtr model;
	keyList keys;
	TRANSAPI_CLBCKS_ORDER_TYPE order;
	struct transapi_data_callbacks *calls;
};

static int transapi_revert_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err** error);
static int transapi_apply_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error);

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
		/* reconnect old node supposed to be removed back to the new XML tree */
		if (tree->node->parent->type != XML_DOCUMENT_NODE) {
			parent = find_element_equiv(info->new, tree->node->parent, info->model, info->keys);
			xmlAddChild(parent, xmlCopyNode(tree->node, 1));
		} else {
			/* we are reconnecting the whole tree */
			xmlnode = xmlDocCopyNode(tree->node, info->new, 1);
			xmlDocSetRootElement(info->new, xmlnode);
		}
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
}

static int transapi_revert_callbacks_recursive_own(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err** error)
{
	xmlNodePtr xmlnode = NULL;
	int ret;
	XMLDIFF_OP op = XMLDIFF_NONE;
	struct nc_err *new_error = NULL;

	if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
		/* process the current node */
		if (tree->priority != 0) {
			if (info->order == TRANSAPI_CLBCKS_LEAF_TO_ROOT) {
				/* discard proposed changes */
				transapi_revert_xml_tree(info, tree);
			} else {
				if (tree->applied == CLBCKS_APPLIED_NONE || tree->applied == CLBCKS_APPLIED_ERROR) {
					/* discard proposed changes */
					transapi_revert_xml_tree(info, tree);
					return (REVERT_CALLBACK_CONTINUE);
				}
			}
		}

	} else if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
		/* process the current node */
		/* current node has no callback function or it was not applied */
		if (!tree->callback || tree->applied == CLBCKS_APPLIED_NONE || tree->applied == CLBCKS_APPLYING_CHILDREN) {
			return (REVERT_CALLBACK_CONTINUE);
		}

		/*
		 * do not affect XML tree, just use transAPI callbacks
		 * to revert applied changes. XML tree is reverted in
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
			xmlnode = find_element_equiv(info->old, tree->node, info->model, info->keys);
			if (xmlnode != NULL ) {
				op = tree->op;
				/* xmlnode already set */
			} else {
				ERROR("Unable to revert executed changes: previous subtree version not found.");
				/* Previous subtree not found: no need to process children nodes */
				return (REVERT_CALLBACK_ERROR);
			}
		}

		DBG("Transapi revert callback %s with op %d.", tree->path, op);

		/* revert changes */
		ret = info->calls->callbacks[tree->priority - 1].func(&info->calls->data, op, xmlnode, &new_error);

		if (ret != EXIT_SUCCESS) {
			WARN("Reverting configuration changes via transAPI failed, configuration may be inconsistent.");
			if (*error != NULL && new_error != NULL) {
				/* concatenate errors */
				new_error->next = *error;
				*error = new_error;
			} else if (new_error != NULL) {
				*error = new_error;
			}
			return (REVERT_CALLBACK_ERROR);
		}
	}
	return (REVERT_CALLBACK_SUCCESS);
}

static void transapi_revert_callbacks_recursive_children(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err** error)
{
	struct xmldiff_tree *child;

	if (erropt == NC_EDIT_ERROPT_NOTSET || erropt == NC_EDIT_ERROPT_STOP) {
		/* do the recursion - process children */
		for(child = tree->children; child != NULL; child = child->next) {
			/*
			 * current node either all its children were applied and we
			 * don't need to revert corresponding XML tree changes
			 */
			if (child->applied == CLBCKS_APPLIED_FULLY) {
				continue;
			}

			/* do the recursion */
			transapi_revert_callbacks_recursive(info, child, erropt, error);
		}
	} else if (erropt == NC_EDIT_ERROPT_ROLLBACK) {
		/* do the recursion - process children */
		for(child = tree->children; child != NULL; child = child->next) {
			/* if priority == 0 then the node neither its children have no callbacks */
			if (child->priority == 0 || child->applied == CLBCKS_APPLIED_NONE) {
				continue;
			}

			/* do the recursion */
			transapi_revert_callbacks_recursive(info, child, erropt, error);
		}
	}
}

static int transapi_revert_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err** error)
{
	int ret = EXIT_SUCCESS;

	if (info->order == TRANSAPI_CLBCKS_LEAF_TO_ROOT) {
		transapi_revert_callbacks_recursive_children(info, tree, erropt, error);
		ret = transapi_revert_callbacks_recursive_own(info, tree, erropt, error);
		if (ret == REVERT_CALLBACK_ERROR)
			return (EXIT_FAILURE);
	} else {
		ret = transapi_revert_callbacks_recursive_own(info, tree, erropt, error);
		if (ret == REVERT_CALLBACK_SUCCESS)
			transapi_revert_callbacks_recursive_children(info, tree, erropt, error);
		else if (ret == REVERT_CALLBACK_ERROR)
			return (EXIT_FAILURE);
	}

	return ret;
}

static int transapi_apply_callbacks_recursive_own(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error) {
	int ret;
	struct nc_err *new_error = NULL;

	if (tree->callback) {
		DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
		ret = info->calls->callbacks[tree->priority-1].func(&info->calls->data, tree->op, tree->node, &new_error);
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
			}

			return (APPLY_CALLBACK_ERROR);
		}
	}

	return (APPLY_CALLBACK_SUCCESS);
}

static int transapi_apply_callbacks_recursive_children(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error)
{
	struct xmldiff_tree* child, *cur_min;
	int retval = APPLY_CALLBACK_SUCCESS;

	do {
		cur_min = NULL;
		child = tree->children;
		while (child != NULL) {
			if (child->priority && child->applied==CLBCKS_APPLIED_NONE) {
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
					return (APPLY_CALLBACK_ERROR);
				}
				/*
				 * on continue-on-error, continue with processing next
				 * change, but remember that we have failed
				 */
				retval = APPLY_CALLBACK_CONTINUE;
			}
		}
	} while (cur_min != NULL);

	return retval;
}

/* call the callbacks in the order set by the priority of each change */
static int transapi_apply_callbacks_recursive(const struct transapi_callbacks_info *info, struct xmldiff_tree* tree, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error)
{
	int retval;

	if (info->order == TRANSAPI_CLBCKS_LEAF_TO_ROOT) {
		tree->applied = CLBCKS_APPLYING_CHILDREN;

		retval = transapi_apply_callbacks_recursive_children(info, tree, erropt, error);
		if (retval == APPLY_CALLBACK_ERROR) {
			return EXIT_FAILURE;
		}

		/* Update applied status */
		if (retval == APPLY_CALLBACK_SUCCESS)
			tree->applied = CLBCKS_APPLIED_CHILDREN_NO_ERROR;
		else
			/* continue on error */
			tree->applied = CLBCKS_APPLIED_CHILDREN_ERROR;

		/* Finally call our callback */
		retval = transapi_apply_callbacks_recursive_own(info, tree, erropt, error);

		if (retval == APPLY_CALLBACK_SUCCESS) {
			if (tree->applied == CLBCKS_APPLIED_CHILDREN_NO_ERROR)
				tree->applied = CLBCKS_APPLIED_FULLY;
			else
				tree->applied = CLBCKS_APPLIED_NOT_FULLY;
		} else {
			tree->applied = CLBCKS_APPLIED_ERROR;
			return (EXIT_FAILURE);
		}
	} else {
		retval = transapi_apply_callbacks_recursive_own(info, tree, erropt, error);
		if (retval == APPLY_CALLBACK_ERROR) {
			tree->applied = CLBCKS_APPLIED_ERROR;
			return EXIT_FAILURE;
		}

		/* Callback applied successfully. Applying children callbacks */
		tree->applied = CLBCKS_APPLYING_CHILDREN;
		retval = transapi_apply_callbacks_recursive_children(info, tree, erropt, error);
		if (retval == APPLY_CALLBACK_ERROR) {
			tree->applied = CLBCKS_APPLIED_NOT_FULLY;
			return EXIT_FAILURE;
		} else if (retval == APPLY_CALLBACK_SUCCESS)
			tree->applied = CLBCKS_APPLIED_FULLY;
		else
			/* continue on error */
			tree->applied = CLBCKS_APPLIED_NOT_FULLY;
	}
	return (EXIT_SUCCESS);
}

/* will be called by library after change in running datastore */
int transapi_running_changed(struct ncds_ds* ds, xmlDocPtr old_doc, xmlDocPtr new_doc, NC_EDIT_ERROPT_TYPE erropt, struct nc_err **error)
{
	struct xmldiff_tree* diff = NULL;
	struct transapi_callbacks_info info;
	
	if (xmldiff_diff(&diff, old_doc, new_doc, ds->ext_model_tree) == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Model \"%s\" transAPI: failed to create the tree of differences.", ds->data_model->name);
		xmldiff_free(diff);
		return EXIT_FAILURE;
	} else if (diff != NULL) {
		if (xmldiff_set_priorities(diff, ds->transapi.data_clbks) != EXIT_SUCCESS) {
			VERB("Model \"%s\" transAPI: there was not a single callback found for the configuration change.", ds->data_model->name);
		} else {
			info.old = old_doc;
			info.new = new_doc;
			info.model = ds->ext_model;
			info.keys = get_keynode_list(info.model);
			info.order = ds->transapi.clbks_order;
			info.calls = ds->transapi.data_clbks;

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
		VERB("Model \"%s\" transAPI: nothing changed.", ds->data_model->name);
	}

	xmldiff_free(diff);
	free(diff);
	return EXIT_SUCCESS;
}
