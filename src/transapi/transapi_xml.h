#ifndef TRANSAPI_XML_H_
#define TRANSAPI_XML_H_

#include <libxml/tree.h>

#include "transapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* maximal number of input arguments every defined RPC can have */
#ifndef MAX_RPC_INPUT_ARGS
#define MAX_RPC_INPUT_ARGS 64
#endif

/**
 * @ingroup transapi
 * @brief Same as transapi_data_callbacks. Using libxml2 structures for callbacks parameters.
 */
struct transapi_xml_data_callbacks {
	int callbacks_count;
	void * data;
	struct {
		char * path;
		int (*func)(XMLDIFF_OP, xmlNodePtr, void **);
	} callbacks[];
};

/**
 * @ingroup transapi
 * @brief Same as transapi_rpc_callbacks. Using libxml2 structures for callbacks parameters.
 */
struct transapi_xml_rpc_callbacks {
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

#endif /* TRANSAPI_XML_H_ */
