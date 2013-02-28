#include "transapi.h"
#include "string.h"

/* will be called by library after change in running datastore */
int transapi_running_changed (struct transapi_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct yinmodel * model)
{
	struct xmldiff * diff;
	int i,j;
	
	if ((diff = xmldiff_diff (old_doc, new_doc, model)) == NULL) {
		return EXIT_FAILURE;
	}

	if (diff->all_stat != XMLDIFF_NONE) {
		for (i=0; i<diff->diff_count; i++) {
			for (j=0; i<c->callbacks_count; j++) {
				if (strcmp(diff->diff_list[i].path, c->callbacks[j].path) == 0) {
					if ((c->callbacks[j].func(diff->diff_list[i].op, diff->diff_list[i].node, &c->data)) != EXIT_SUCCESS) {
						xmldiff_free (diff);
						return EXIT_FAILURE;
					}
					break;
				}
			}
		}
	}

	xmldiff_free (diff);
	return EXIT_SUCCESS;
}
