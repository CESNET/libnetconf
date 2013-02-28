#ifndef _YINPARSER_H
#define _YINPARSER_H

#include <libxml/tree.h>
/* enum type for yin/yang constructs */
typedef enum {YIN_TYPE_MODULE, YIN_TYPE_CONTAINER, YIN_TYPE_LEAF, YIN_TYPE_LIST, YIN_TYPE_LEAFLIST, YIN_TYPE_CHOICE, YIN_TYPE_ANYXML, YIN_TYPE_GROUPING, YIN_TYPE_IMPORT} YIN_TYPE;

/* structure holding information needed for xml document comparsion */
struct yinmodel {
	YIN_TYPE type;
	char * name;
	char ** keys;
	char * ns_prefix;
	char * ns_uri;
	struct yinmodel * children;
	int keys_count;
	int children_count;
};

/**
 * @brief Parse YIN data model
 *
 * @param model_doc	Data model in YIN format.
 *
 * @return yinmodel structure or NULL
 */
struct yinmodel * yinmodel_parse (xmlDocPtr model_doc);

/**
 * @brief Destroy yinmodel structure and free allocated memory.
 *
 * @param yin	Structure to be freed.
 */
void yinmodel_free (struct yinmodel * yin);

#endif
