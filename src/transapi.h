#ifndef _TRANSAPI_H
#define _TRANSAPI_H

#include <libxml/tree.h>
#include "netconf.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Current transAPI version */
#define TRANSAPI_VERSION 3

/* maximal number of input arguments every defined RPC can have */
#ifndef MAX_RPC_INPUT_ARGS
#define MAX_RPC_INPUT_ARGS 64
#endif

/**
 * @ingroup transapi
 * @brief Enum specifying states of node in document
 */
typedef enum
{
	XMLDIFF_ERR = -1 /**< Error while creating XML difftree. */,
	XMLDIFF_NONE = 0 /**< Last operation did not cause any change in configuration. */,
	XMLDIFF_ADD = 1 /**< Element was added to configuration. */,
	XMLDIFF_REM = 2 /**< Element was removed from configuration. */,
	XMLDIFF_MOD = 4/**< Element was modified. */,
	XMLDIFF_CHAIN = 8/**< Some of children of element was modified/added/removed. */,
	XMLDIFF_SIBLING = 16 /**< Some sibling nodes were added/removed/changed position. Only for LEAF and LEAF-LIST. */,
	XMLDIFF_REORDER = 32 /**< Some of the children nodes changed theirs position. None was added/removed. Only for LEAF and LEAF-LIST. */,
} XMLDIFF_OP;

/**
 * @ingroup transapi
 * @brief Structure to describe transAPI module and connect it statically with
 * libnetconf using ncds_new_transapi_static().
 */
struct transapi {
	/**
	 * @brief Module initialization.
	 */
	int (*init)(xmlDocPtr *);
	/**
	 * @brief Free module resources and prepare for closing.
	 */
	void (*close)(void);
	/**
	 * @brief Transapi callback mapping structure.
	 */
	struct transapi_data_callbacks * data_clbks;
	/**
	 * @brief Transapi rpc callbacks mapping structure.
	 */
	struct transapi_rpc_callbacks * rpc_clbks;
	/**
	 * @brief Mapping prefixes with URIs
	 */
	const char ** ns_mapping;
	/**
	 * @brief Flag if configuration data passed to callbacks were modified
	 */
	int *config_modified;
	/**
	 * @brief edit-config's error-option for the current transaction
	 */
	NC_EDIT_ERROPT_TYPE *erropt;
	/**
	 * @brief Function returning the module status information.
	 */
	xmlDocPtr (*get_state)(const xmlDocPtr, const xmlDocPtr, struct nc_err **);
};

/**
 * @ingroup transapi
 * @brief Same as transapi_data_callbacks. Using libxml2 structures for callbacks parameters.
 */
struct transapi_data_callbacks {
	int callbacks_count;
	void * data;
	struct {
		char * path;
		int (*func)(void**, XMLDIFF_OP, xmlNodePtr, struct nc_err**);
	} callbacks[];
};

/**
 * @ingroup transapi
 * @brief Same as transapi_rpc_callbacks. Using libxml2 structures for callbacks parameters.
 */
struct transapi_rpc_callbacks {
	int callbacks_count;
	struct {
		char * name;
		int arg_count;
		nc_reply * (*func)(xmlNodePtr []);
		char * arg_order[MAX_RPC_INPUT_ARGS];
	} callbacks[];
};

#ifdef __cplusplus
}
#endif

#endif
