#ifndef _XMLDIFF_H
#define _XMLDIFF_H

#include <libxml/tree.h>
#include "yinparser.h"
#include "transapi.h"

/**
 * @ingroup transapi
 * @brief tree structure holding all differencies found in compared files
 */
struct xmldiff_tree {
	char* path;
	xmlNodePtr node;
	XMLDIFF_OP op;

	struct xmldiff_tree* next;
	struct xmldiff_tree* parent;
	struct xmldiff_tree* children;
};

/**
 * @ingroup transapi
 * @brief Destroy and free whole xmldiff structure
 *
 * @param diff	pointer to xmldiff structure
 */
void xmldiff_free (struct xmldiff_tree* diff);

/**
 * @ingroup transapi
 * @brief Module top level function
 *
 * @param old		old version of XML document
 * @param new		new version of XML document
 * @param model	data model in YANG format
 * @param ns_mapping Pairing prefixes with URIs
 *
 * @return xmldiff structure holding all differences between XML documents or NULL
 */
struct xmldiff_tree* xmldiff_diff (xmlDocPtr old, xmlDocPtr new, struct model_tree * model, const char * ns_mapping[]);

#endif
