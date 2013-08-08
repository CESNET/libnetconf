
#include <string.h>
#include <stdio.h>

#include "transapi_internal.h"
#include "xmldiff.h"
#include "../netconf_internal.h"

/* will be called by library after change in running datastore */
int transapi_xml_running_changed (struct transapi_xml_data_callbacks * c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff * diff;
	int i,j, ret;
	char * last_slash = NULL, * parent_path = NULL, * tmp_path;
	
	if ((diff = xmldiff_diff (old_doc, new_doc, model, ns_mapping)) == NULL || diff->all_stat == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Failed to create list of differences.\n");
		xmldiff_free (diff);
		return EXIT_FAILURE;
	} else if (diff->all_stat != XMLDIFF_NONE) {
		for (i=0; i<diff->diff_count; i++) { /* for each diff*/
			DBG("(%d) %s\n", diff->diff_list[i].op, diff->diff_list[i].path);
			for (j=0; j<c->callbacks_count; j++) { /* find callback function */
				if (strcmp(diff->diff_list[i].path, c->callbacks[j].path) == 0) { /* exact match */
					/* call callback function */
					if ((ret = c->callbacks[j].func(diff->diff_list[i].op, diff->diff_list[i].node, &c->data)) != EXIT_SUCCESS) {
						ERROR("Callback for path %s failed (%d).\n", diff->diff_list[i].path, ret);
						xmldiff_free (diff);
						return EXIT_FAILURE;
					}
					break;
				}
			}
			if (j == c->callbacks_count) { /* no callback function for given path found */
				VERB("Path %s(%d/%d) has no callback\n", diff->diff_list[i].path, i, diff->diff_count);
				//~ /* add _MOD flag to nearest ancestor to ensure application of configuration changes*/
				//~ tmp_path = strdup(diff->diff_list[i].path);
				//~ while (!found && strlen(tmp_path) > 1) {
					//~ /* find last "/" in path */
					//~ if ((last_slash = rindex (tmp_path, '/')) == NULL) {
						//~ /* every path MUST contain at least one slash */
						//~ xmldiff_free (diff);
						//~ return EXIT_FAILURE;
					//~ }
					//~ /* get parent path */
					//~ parent_path = strndup(tmp_path, last_slash - tmp_path);
					//~ free(tmp_path);
					//~ for (j=i; j<diff->diff_count; j++) { /* find nearest ancestor */
						//~ if (strcmp (parent_path, diff->diff_list[j].path) == 0) {
							//~ diff->diff_list[j].op |= XMLDIFF_MOD; /* mark it as MODified */
							//~ found = 1;
							//~ break;
						//~ }
					//~ }
					//~ tmp_path = parent_path;
				//~ }
				//~ free(tmp_path);
				//~ if (found == 0) {
					//~ WARN("Changes in path %s will not be applied due to missing callback function.\n", diff->diff_list[i].path);
				//~ } else {
					//~ WARN("Changes in path %s should be applied when calling callback for path %s.\n", diff->diff_list[i].path, diff->diff_list[j].path);
				//~ }
				//~ found = 0;
			}
		}
	} else {
		VERB("Nothing changed.\n");
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}

/* will be called by library after change in running datastore */
int transapi_running_changed (struct transapi_data_callbacks * c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model)
{
	struct xmldiff * diff;
	int i,j, ret;
	char * last_slash = NULL, * parent_path, *tmp_path = NULL;
	xmlBufferPtr buf;
	char * node;

	if ((diff = xmldiff_diff (old_doc, new_doc, model, ns_mapping)) == NULL || diff->all_stat == XMLDIFF_ERR) { /* failed to create diff list */
		ERROR("Failed to create list of differences.\n");
		xmldiff_free (diff);
		return EXIT_FAILURE;
	} else if (diff->all_stat != XMLDIFF_NONE) {
		buf = xmlBufferCreate();
		for (i=0; i<diff->diff_count; i++) { /* for each diff*/
			DBG("(%d) %s\n", diff->diff_list[i].op, diff->diff_list[i].path);
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
				VERB("Path %s(%d/%d) has no callback\n", diff->diff_list[i].path, i, diff->diff_count);
				//~ /* add _MOD flag to parent to ensure application of configuration changes*/
				//~ tmp_path = strdup(diff->diff_list[i].path);
				//~ while (!found && strlen(tmp_path) > 1) {
					//~ /* find last "/" in path */
					//~ if ((last_slash = rindex (diff->diff_list[i].path, '/')) == NULL) {
						//~ /* every path MUST contain at least one slash */
						//~ xmldiff_free (diff);
						//~ return EXIT_FAILURE;
					//~ }
					//~ /* get parent path */
					//~ parent_path = strndup(tmp_path, last_slash - tmp_path);
					//~ free(tmp_path);
					//~ for (j=i; j<diff->diff_count; j++) { /* find parent */
						//~ if (strcmp (parent_path, diff->diff_list[j].path) == 0) {
							//~ diff->diff_list[j].op |= XMLDIFF_MOD; /* mark it as MODified */
							//~ found = 1;
							//~ break;
						//~ }
					//~ }
					//~ tmp_path = parent_path;
				//~ }
				//~ free(tmp_path);
				//~ if (found == 0) {
					//~ WARN("Changes in path %s will not be applied due to missing callback function.\n", diff->diff_list[i].path);
				//~ } else {
					//~ WARN("Changes in path %s should be applied when calling callback for path %s.\n", diff->diff_list[i].path, diff->diff_list[j].path);
				//~ }
				//~ found = 0;
			}
		}
		xmlBufferFree(buf);
	} else {
		VERB("Nothing changed.\n");
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}
