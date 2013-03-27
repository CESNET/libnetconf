#ifndef _TRANSAPI_H
#define _TRANSAPI_H

#include "xmldiff.h"
#include "yinparser.h"

/**
 * @ingroup transapi
 * @brief Structure binding location in configuration XML data and function callback applying changes.
 */
struct transapi_config_callbacks {
	int callbacks_count;
	void * data;
	struct {
		char * path;
		int (*func)(XMLDIFF_OP, xmlNodePtr, void **);
	} callbacks[];
};

/**
 * @ingroup transapi
 * @brief Top level function of transaction API. Finds differences between old_doc and new_doc and calls specified callbacks.
 *
 * @param[in] c Structure binding callbacks with paths in XML document
 * @param[in] old_doc Content of configuration datastore before change.
 * @param[in] new_doc Content of configuration datastore after change.
 * @param[in] model Structure holding document semantics.
 *
 * @return EXIT_SUCESS or EXIT_FAILURE
 */
int transapi_running_changed (struct transapi_config_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct yinmodel * model);

#endif
