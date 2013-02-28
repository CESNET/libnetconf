#ifndef _TRANSAPI_H
#define _TRANSAPI_H

#include "xmldiff.h"
#include "yinparser.h"

struct transapi_callbacks {
	int callbacks_count;
	void * data;
	struct {
		char * path;
		int (*func)(XMLDIFF_OP, xmlNodePtr, void **);
	} callbacks[];
};

int transapi_running_changed (struct transapi_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct yinmodel * model);

#endif
