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
		int (*func)(XMLDIFF_OP, char *, void **);
	} callbacks[];
};


/**
 * @ingroup transapi
 * @brief Structure binding location in configuration XML data and function callback processing RPC message.
 */
struct transapi_rpc_callbacks {
        int callbacks_count;
        struct {
                char * name;
                int arg_count;
        		nc_reply * (*func)(char *[]);
                char * arg_order[MAX_RPC_INPUT_ARGS];
        } callbacks[];
};

#endif
