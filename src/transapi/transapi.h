#ifndef _TRANSAPI_H
#define _TRANSAPI_H

#include "xmldiff.h"
#include "yinparser.h"
#include "../libnetconf.h"

/* maximal number of input arguments every defined RPC can have */
#ifndef MAX_RPC_INPUT_ARGS
#define MAX_RPC_INPUT_ARGS 64
#endif

/**
 * @ingroup transapi
 * @brief Structure binding location in configuration XML data and function callback applying changes.
 */
struct transapi_data_callbacks {
	int callbacks_count;
	void * data;
	struct {
		char * path;
		int (*func)(XMLDIFF_OP, xmlNodePtr, void **);
	} callbacks[];
};


struct transapi_rpc_callbacks {
        int callbacks_count;
        struct {
                char * name;
                int arg_count;
                nc_reply * (*func) ();
                char * arg_order[MAX_RPC_INPUT_ARGS];
        } callbacks[];
};

struct transapi {
	/**
	 * @brief Loaded shared library with transapi callbacks.
	 */
	void * module;
	/**
	 * @brief Transapi callback mapping structure.
	 */
	struct transapi_data_callbacks * data_clbks;
	/**
	 * @brief Transapi rpc callbacks mapping structure.
	 */
	struct transapi_rpc_callbacks * rpc_clbks;
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
int transapi_running_changed (struct transapi_data_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model);

#endif
