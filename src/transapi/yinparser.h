#ifndef _YINPARSER_H
#define _YINPARSER_H

#include <libxml/tree.h>
/**
 * @ingroup transapi
 * @brief enum type for yin/yang constructs
 */
typedef enum {
	YIN_TYPE_MODULE,
	YIN_TYPE_CONTAINER,
	YIN_TYPE_LEAF,
	YIN_TYPE_LIST,
	YIN_TYPE_LEAFLIST,
	YIN_TYPE_CHOICE,
	YIN_TYPE_ANYXML,
	YIN_TYPE_GROUPING,
	YIN_TYPE_IMPORT,
	YIN_TYPE_AUGMENT
} YIN_TYPE;

/**
 * @ingroup transapi
 * @brief structure holding information about used data model in YIN format
 */
struct model_tree {
	YIN_TYPE type;
	char * name;
	char ** keys;
	char * ns_uri;
	char * ns_prefix;
	struct model_tree * children;
	int keys_count;
	int children_count;
};

/**
 * @ingroup transapi
 * @brief Parse YIN data model
 *
 * @param model_doc	Data model in YIN format.
 * @param ns_mapping Pairing prefixes with URIs.
 *
 * @return yinmodel structure or NULL
 */
struct model_tree * yinmodel_parse (xmlDocPtr model_doc, const char * ns_mapping[]);

/**
 * @ingroup transapi
 * @brief Destroy yinmodel structure and free allocated memory.
 *
 * @param yin	Structure to be freed.
 */
void yinmodel_free (struct model_tree * yin);

#endif
