#ifndef _YINPARSER_H
#define _YINPARSER_H

#include <libxml/tree.h>
/**
 * @ingroup transapi
 * @brief enum type for yin/yang constructs
 */
typedef enum {
	YIN_TYPE_MODULE, /**< Top of the YANG data model (http://tools.ietf.org/html/rfc6020#section-7.1) */
	YIN_TYPE_CONTAINER, /**< YANG container statement (http://tools.ietf.org/html/rfc6020#section-7.5) */
	YIN_TYPE_LEAF, /**< YANG leaf statement (http://tools.ietf.org/html/rfc6020#section-7.6) */
	YIN_TYPE_LIST, /**< YANG list statement (https://tools.ietf.org/html/rfc6020#section-7.8) */
	YIN_TYPE_LEAFLIST, /**< YANG leaf-list statement (http://tools.ietf.org/html/rfc6020#section-7.7) */
	YIN_TYPE_CHOICE, /**< YANG choice statement (https://tools.ietf.org/html/rfc6020#section-7.9) */
	YIN_TYPE_ANYXML, /**< YANG anyxml statement (https://tools.ietf.org/html/rfc6020#section-7.10) */
	YIN_TYPE_GROUPING, /**< YANG grouping statement (https://tools.ietf.org/html/rfc6020#section-7.11) */
	YIN_TYPE_IMPORT, /**< YANG import statement (https://tools.ietf.org/html/rfc6020#section-7.17.2) */
	YIN_TYPE_AUGMENT /**< YANG augment statement (https://tools.ietf.org/html/rfc6020#section-7.15) */
} YIN_TYPE;

typedef enum {
	YIN_ORDER_SYSTEM,
	YIN_ORDER_USER
} YIN_ORDER;

/**
 * @ingroup transapi
 * @brief structure holding information about used data model in YIN format
 */
struct model_tree {
	YIN_TYPE type;
	YIN_ORDER ordering; /** < list ordering valid only when type=={YIN_TYPE_LIST|YIN_TYPE_LEAFLIST} */
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
