#include "transapi_internal.h"
#include <string.h>
#include <stdio.h>

/* will be called by library after change in running datastore */
int transapi_xml_running_changed (struct transapi_xml_data_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff * diff;
	int i,j, ret;
	char * last_slash = NULL, * parent_path;
	
	if ((diff = xmldiff_diff (old_doc, new_doc, model)) == NULL || diff->all_stat == XMLDIFF_ERR) { /* failed to create diff list */
		xmldiff_free (diff);
		return EXIT_FAILURE;
	} else if (diff->all_stat != XMLDIFF_NONE) {
		for (i=0; i<diff->diff_count; i++) { /* for each diff*/
			for (j=0; j<c->callbacks_count; j++) { /* find callback function */
				if (strcmp(diff->diff_list[i].path, c->callbacks[j].path) == 0) { /* exact match */
					/* call callback function */
					if ((ret = c->callbacks[j].func(diff->diff_list[i].op, diff->diff_list[i].node, &c->data)) != EXIT_SUCCESS) {
						xmldiff_free (diff);
						return EXIT_FAILURE;
					}
					break;
				}
			}
			if (j == c->callbacks_count) { /* no callback function for given path found */
				/* add _MOD flag to parent to ensure application of configuration changes*/
				/* find last "/" in path */
				if ((last_slash = rindex (diff->diff_list[i].path, '/')) == NULL) {
					/* every path MUST contain at least one slash */
					xmldiff_free (diff);
					return EXIT_FAILURE;
				}
				/* get parent path */
				parent_path = strndup(diff->diff_list[i].path, last_slash - diff->diff_list[i].path);
				for (j=i; j<diff->diff_count; j++) { /* find parent */
					if (strcmp (parent_path, diff->diff_list[j].path) == 0) {
						diff->diff_list[j].op |= XMLDIFF_MOD; /* mark it as MODified */
						break;
					}
				}
				free (parent_path);
			}
		}
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}

/* will be called by library after change in running datastore */
int transapi_running_changed (struct transapi_data_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff * diff;
	int i,j, ret;
	char * last_slash = NULL, * parent_path;
	xmlBufferPtr buf;
	char * node;

	if ((diff = xmldiff_diff (old_doc, new_doc, model)) == NULL || diff->all_stat == XMLDIFF_ERR) { /* failed to create diff list */
		xmldiff_free (diff);
		return EXIT_FAILURE;
	} else if (diff->all_stat != XMLDIFF_NONE) {
		buf = xmlBufferCreate();
		for (i=0; i<diff->diff_count; i++) { /* for each diff*/
			for (j=0; j<c->callbacks_count; j++) { /* find callback function */
				if (strcmp(diff->diff_list[i].path, c->callbacks[j].path) == 0) { /* exact match */
					/* call callback function */
					/* if node was removed, it was copied from old XML doc, else from new XML doc */
					xmlNodeDump(buf,diff->diff_list[i].op == XMLDIFF_REM ? old_doc : new_doc, diff->diff_list[i].node, 1, 0);
					node = (char*)xmlBufferContent(buf);
					if ((ret = c->callbacks[j].func(diff->diff_list[i].op, node, &c->data)) != EXIT_SUCCESS) {
						xmldiff_free (diff);
						return EXIT_FAILURE;
					}
					xmlBufferEmpty(buf);
					break;
				}
			}
			if (j == c->callbacks_count) { /* no callback function for given path found */
				/* add _MOD flag to parent to ensure application of configuration changes*/
				/* find last "/" in path */
				if ((last_slash = rindex (diff->diff_list[i].path, '/')) == NULL) {
					/* every path MUST contain at least one slash */
					xmldiff_free (diff);
					return EXIT_FAILURE;
				}
				/* get parent path */
				parent_path = strndup(diff->diff_list[i].path, last_slash - diff->diff_list[i].path);
				for (j=i; j<diff->diff_count; j++) { /* find parent */
					if (strcmp (parent_path, diff->diff_list[j].path) == 0) {
						diff->diff_list[j].op |= XMLDIFF_MOD; /* mark it as MODified */
						break;
					}
				}
				free (parent_path);
			}
		}
		xmlBufferFree(buf);
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}
