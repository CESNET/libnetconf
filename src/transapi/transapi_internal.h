#ifndef TRANSAPI_INTERNAL_H_
#define TRANSAPI_INTERNAL_H_

#include "transapi.h"
#include "transapi_xml.h"
#include "yinparser.h"
#include "../datastore/datastore_internal.h"

/**
 * @ingroup transapi
 * @brief Enum with XML relationships between the nodes
 */
typedef enum
{
	XML_PARENT, /**< Represent XML parent role. */
	XML_CHILD, /**< Represent XML child role. */
	XML_SIBLING /**< Represent XML sibling role. */
} XML_RELATION;

/**
 * @ingroup transapi
 * @brief Top level function of transaction API. Finds differences between old_doc and new_doc and calls specified callbacks.
 *
 * @param[in] c Structure binding callbacks with paths in XML document
 * @param[in] ns_mapping Pairing prefixes with URIs.
 * @param[in] old_doc Content of configuration datastore before change.
 * @param[in] new_doc Content of configuration datastore after change.
 * @param[in] model Structure holding document semantics.
 * @param[in] libxml2 Specify if the module uses libxml2 API
 *
 * @return EXIT_SUCESS or EXIT_FAILURE
 */
int transapi_running_changed(void* c, const char * ns_mapping[], xmlDocPtr old_doc, xmlDocPtr new_doc, struct data_model *model, NC_EDIT_ERROPT_TYPE erropt, int libxml2, struct nc_err **error);

#endif /* TRANSAPI_INTERNAL_H_ */
