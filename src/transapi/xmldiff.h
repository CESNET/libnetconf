#ifndef _XMLDIFF_H
#define _XMLDIFF_H

#include <stdbool.h>
#include <libxml/tree.h>
#include "yinparser.h"
#include "transapi.h"

struct xmldiff_prio {
	int* values;
	size_t used;
	size_t alloc;
};

/**
 * @ingroup transapi
 * @brief tree structure holding all differencies found in compared files
 */
struct xmldiff_tree {
	char* path;
	xmlNodePtr node;
	XMLDIFF_OP op;

	int priority;
	bool applied;

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
XMLDIFF_OP xmldiff_diff (struct xmldiff_tree** diff, xmlDocPtr old, xmlDocPtr new, struct model_tree * model, const char * ns_mapping[]);

int xmldiff_set_priorities(struct xmldiff_tree* tree, void* callbacks);

#endif
