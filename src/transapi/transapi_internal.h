#ifndef TRANSAPI_INTERNAL_H_
#define TRANSAPI_INTERNAL_H_

#include "transapi.h"
#include "transapi_xml.h"
#include "yinparser.h"

union transapi_data_clbcks {
	struct transapi_data_callbacks * data_clbks;
	struct transapi_xml_data_callbacks * data_clbks_xml;
};

union transapi_rpc_clbcks {
	struct transapi_rpc_callbacks * rpc_clbks;
	struct transapi_xml_rpc_callbacks * rpc_clbks_xml;
};

union transapi_init {
	xmlDocPtr (*init_xml)(xmlDocPtr startup_config);
	char * (*init)(char * startup_config);
};

struct transapi {
	/**
	 * @brief Loaded shared library with transapi callbacks.
	 */
	void * module;
	/**
	 * @brief Does module support libxml2?
	 */
	int libxml2;
	/**
	 * @brief Transapi callback mapping structure.
	 */
	union transapi_data_clbcks data_clbks;
	/**
	 * @brief Transapi rpc callbacks mapping structure.
	 */
	union transapi_rpc_clbcks rpc_clbks;
	/**
	 * @brief Module initialization.
	 */
	union transapi_init init;
	/**
	 * @brief Free module resources and prepare for closing.
	 */
	void (*close)(void);
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

/**
 * @ingroup transapi
 * @brief Same functionality as transapi_running_changed(). Using libxml2 structures for callbacks parameters.
 *
 * @param[in] c Structure binding callbacks with paths in XML document. Callbacks uses libxml2 structures.
 * @param[in] old_doc Content of configuration datastore before change.
 * @param[in] new_doc Content of configuration datastore after change.
 * @param[in] model Structure holding document semantics.
 *
 * @return EXIT_SUCESS or EXIT_FAILURE
 */
int transapi_xml_running_changed (struct transapi_xml_data_callbacks * c, xmlDocPtr old_doc, xmlDocPtr new_doc, struct model_tree * model);

#endif /* TRANSAPI_INTERNAL_H_ */
