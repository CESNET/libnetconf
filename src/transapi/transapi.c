#include <string.h>
#include <stdio.h>

#include "transapi_internal.h"
#include "xmldiff.h"
#include "../netconf_internal.h"

int transapi_xml_apply_callbacks_recursive(struct xmldiff_tree* tree, struct transapi_xml_data_callbacks* calls) {
	struct xmldiff_tree* child;
	int min_prio, ret;

	do {
		min_prio = 0;
		child = tree->children;
		while (child != NULL) {
			if (min_prio == 0 && !child->applied && child->priority > 0) {
				/* Set minimal priority with a reasonable value (first child's priority) */
				min_prio = child->priority;
				child = child->next;
				continue;
			}

			if (!child->applied && child->priority > 0 && child->priority < min_prio) {
				min_prio = child->priority;
			}
			child = child->next;
		}

		if (min_prio > 0) {
			child = tree->children;
			while (child != NULL) {
				if (!child->applied && child->priority == min_prio) {
					/* Process this child recursively */
					if (transapi_xml_apply_callbacks_recursive(child, calls) != EXIT_SUCCESS) {
						return EXIT_FAILURE;
					}
					break;
				}
				child = child->next;
			}
		}
	} while (min_prio > 0);

	/* Finally call our callback */
	DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
	ret = calls->callbacks[tree->priority-1].func(tree->op, tree->node, &calls->data);
	tree->applied = true;
	if (ret != EXIT_SUCCESS) {
		ERROR("Callback for path %s failed (%d).", tree->path, ret);
	}

	return ret;
}

int transapi_apply_callbacks_recursive(struct xmldiff_tree* tree, struct transapi_data_callbacks* calls, xmlDocPtr old_doc, xmlDocPtr new_doc) {
	struct xmldiff_tree* child;
	int min_prio, ret;
	xmlBufferPtr buf;
	char* node;

	do {
		min_prio = 0;
		child = tree->children;
		while (child != NULL) {
			if (min_prio == 0 && !child->applied && child->priority > 0) {
				/* Set minimal priority with a reasonable value (first child's priority) */
				min_prio = child->priority;
				child = child->next;
				continue;
			}

			if (!child->applied && child->priority > 0 && child->priority < min_prio) {
				min_prio = child->priority;
			}
			child = child->next;
		}

		if (min_prio > 0) {
			child = tree->children;
			while (child != NULL) {
				if (!child->applied && child->priority == min_prio) {
					/* Process this child recursively */
					if (transapi_xml_apply_callbacks_recursive(child, calls) != EXIT_SUCCESS) {
						return EXIT_FAILURE;
					}
					break;
				}
				child = child->next;
			}
		}
	} while (min_prio > 0);

	/* Finally call our callback */
	DBG("Transapi calling callback %s with op %d.", tree->path, tree->op);
	/* if node was removed, it was copied from old XML doc, else from new XML doc */
	xmlNodeDump(buf, tree->op == XMLDIFF_REM ? old_doc : new_doc, tree->node, 1, 0);
	node = (char*)xmlBufferContent(buf);
	ret = calls->callbacks[tree->priority-1].func(tree->op, node, &calls->data);
	xmlBufferEmpty(buf);
	tree->applied = true;
	if (ret != EXIT_SUCCESS) {
		ERROR("Callback for path %s failed (%d).", tree->path, ret);
	}

	return ret;
}

/* will be called by library after change in running datastore */
int transapi_xml_running_changed (struct transapi_xml_data_callbacks * c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff_tree* diff = NULL;
	
	if (xmldiff_diff(&diff, old_doc, new_doc, model, ns_mapping) == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Failed to create the tree of differences.");
		xmldiff_free(diff);
		return EXIT_FAILURE;
	} else if (diff != NULL) {
		if (xmldiff_set_priorities(diff, c) != EXIT_SUCCESS) {
			VERB("There was not found a single callback for the configuration change.");
		} else {
			if (transapi_xml_apply_callbacks_recursive(diff, c) != EXIT_SUCCESS) {
				xmldiff_free(diff);
				return EXIT_FAILURE;
			}
		}
	} else {
		VERB("Nothing changed.");
	}

	xmldiff_free(diff);
	return EXIT_SUCCESS;
}

/* will be called by library after change in running datastore */
int transapi_running_changed (struct transapi_data_callbacks * c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff_tree* diff = NULL;
	int i,j, ret;
	char * last_slash = NULL, * parent_path, *tmp_path = NULL;
	xmlBufferPtr buf;
	char * node;

	if (xmldiff_diff (&diff, old_doc, new_doc, model, ns_mapping) == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Failed to create the tree of differences.");
		xmldiff_free (diff);
		return EXIT_FAILURE;
	} else if (diff != NULL) {
		if (xmldiff_set_priorities(diff, c) != EXIT_SUCCESS) {
			VERB("There was not found a single callback for the configuration change.");
		} else {
			if (transapi_apply_callbacks_recursive(diff, c, old_doc, new_doc) != EXIT_SUCCESS) {
				xmldiff_free(diff);
				return EXIT_FAILURE;
			}
		}
	} else {
		VERB("Nothing changed.\n");
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}
