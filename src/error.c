/**
 * \file error.c
 * \author <name> <email>
 * \brief <Idea of what it does>
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "error.h"
#include "netconf_internal.h"

/**
 * @brief NETCONF error structure representation
 * @ingroup internalAPI
 */
struct nc_err {
	/**
	 * @brief Error tag
	 *
	 * Expected values are
	 * <br/>#NC_ERR_TAG_IN_USE,<br/>#NC_ERR_TAG_INVALID_VALUE,
	 * <br/>#NC_ERR_TAG_TOO_BIG,<br/>#NC_ERR_TAG_MISSING_ATTR,
	 * <br/>#NC_ERR_TAG_BAD_ATTR,<br/>#NC_ERR_TAG_UNKN_ATTR,
	 * <br/>#NC_ERR_TAG_MISSING_ELEM,<br/>#NC_ERR_TAG_BAD_ELEM,
	 * <br/>#NC_ERR_TAG_UNKN_ELEM,<br/>#NC_ERR_TAG_UNKN_NAMESPACE,
	 * <br/>#NC_ERR_TAG_ACCESS_DENIED,<br/>#NC_ERR_TAG_LOCK_DENIED,
	 * <br/>#NC_ERR_TAG_RES_DENIED,<br/>#NC_ERR_TAG_ROLLBCK,
	 * <br/>#NC_ERR_TAG_DATA_EXISTS,<br/>#NC_ERR_TAG_DATA_MISSING,
	 * <br/>#NC_ERR_TAG_OP_NOT_SUPPORTED,<br/>#NC_ERR_TAG_OP_FAILED,
	 * <br/>#NC_ERR_TAG_PARTIAL_OP,<br/>#NC_ERR_TAG_MALFORMED_MSG.
	 */
	char *tag;
	/**
	 * @brief Error layer where the error occurred
	 *
	 * Expected values are
	 * <br/>#NC_ERR_TYPE_RPC,<br/>#NC_ERR_TYPE_PROT,
	 * <br/>#NC_ERR_TYPE_APP,<br/>#NC_ERR_TYPE_TRANS.
	 */
	char *type;
	/**
	 * @brief Error severity.
	 *
	 * Expected values are
	 * <br/>#NC_ERR_SEV_ERR,<br/>#NC_ERR_SEV_WARN.
	 */
	char *severity;
	/**
	 * @brief The data-model-specific or implementation-specific error condition, if one exists.
	 */
	char *apptag;
	/**
	 * @brief XPATH expression identifying element with error.
	 */
	char *path;
	/**
	 * @brief Human description of the error.
	 */
	char *message;
	/**
	 * @brief Name of the data-model-specific XML attribute that caused the error.
	 *
	 * This information is part of error-info element.
	 */
	char *attribute;
	/**
	 * @brief Name of the data-model-specific XML element that caused the error.
	 *
	 * This information is part of error-info element.
	 */
	char *element;
	/**
	 * @brief Name of the unexpected XML namespace that caused the error.
	 *
	 * This information is part of error-info element.
	 */
	char *ns;
	/**
	 * @brief Session ID of session holding requested lock.
	 *
	 * This information is part of error-info element.
	 */
	char *sid;
};

struct nc_err* nc_err_new(NC_ERR error)
{
	struct nc_err* err = NULL;

	err = calloc(1, sizeof(struct nc_err));
	if (err == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* set error structure according to some predefined errors in RFC 6241 */
	switch (error) {
	case NC_ERR_EMPTY:
		/* do nothing */
		break;
	case NC_ERR_IN_USE:
		nc_err_set(err, NC_ERR_PARAM_TAG, "in-use");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "The request requires a resource that already is in use.");
		break;
	case NC_ERR_INVALID_VALUE:
		nc_err_set(err, NC_ERR_PARAM_TAG, "invalid-value");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "The request specifies an unacceptable value for one or more parameters.");
		break;
	case NC_ERR_TOO_BIG:
		nc_err_set(err, NC_ERR_PARAM_TAG, "too-big");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "The request or response is too large for the implementation to handle.");
		break;
	case NC_ERR_MISSING_ATTR:
		nc_err_set(err, NC_ERR_PARAM_TAG, "missing-attribute");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An expected attribute is missing.");
		break;
	case NC_ERR_BAD_ATTR:
		nc_err_set(err, NC_ERR_PARAM_TAG, "bad-attribute");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An attribute value is not correct.");
		break;
	case NC_ERR_UNKNOWN_ATTR:
		nc_err_set(err, NC_ERR_PARAM_TAG, "unknown-attribute");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An unexpected attribute is present.");
		break;
	case NC_ERR_MISSING_ELEM:
		nc_err_set(err, NC_ERR_PARAM_TAG, "missing-element");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An expected element is missing.");
		break;
	case NC_ERR_BAD_ELEM:
		nc_err_set(err, NC_ERR_PARAM_TAG, "bad-element");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An element value is not correct.");
		break;
	case NC_ERR_UNKNOWN_ELEM:
		nc_err_set(err, NC_ERR_PARAM_TAG, "unknown-element");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An unexpected element is present.");
		break;
	case NC_ERR_UNKNOWN_NS:
		nc_err_set(err, NC_ERR_PARAM_TAG, "unknown-namespace");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "An unexpected namespace is present.");
		break;
	case NC_ERR_ACCESS_DENIED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "access-denied");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Access to the requested protocol operation or data model is denied because authorization failed.");
		break;
	case NC_ERR_LOCK_DENIED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "lock-denied");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Access to the requested lock is denied because the lock is currently held by another entity.");
		break;
	case NC_ERR_RES_DENIED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "resource-denied");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Request could not be completed because of insufficient resources.");
		break;
	case NC_ERR_ROLLBACK_FAILED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "rollback-failed");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Request to roll back some configuration change was not completed for some reason.");
		break;
	case NC_ERR_DATA_EXISTS:
		nc_err_set(err, NC_ERR_PARAM_TAG, "data-exists");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Request could not be completed because the relevant data model content already exists.");
		break;
	case NC_ERR_DATA_MISSING:
		nc_err_set(err, NC_ERR_PARAM_TAG, "data-missing");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Request could not be completed because the relevant data model content does not exist.");
		break;
	case NC_ERR_OP_NOT_SUPPORTED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "operation-not-supported");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "Request could not be completed because the requested operation is not supported by this implementation.");
		break;
	case NC_ERR_OP_FAILED:
		nc_err_set(err, NC_ERR_PARAM_TAG, "operation-failed");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "application");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "The request requires a resource that already is in use.");
		break;
	case NC_ERR_MALFORMED_MSG:
		nc_err_set(err, NC_ERR_PARAM_TAG, "malformed-message");
		nc_err_set(err, NC_ERR_PARAM_TYPE, "rpc");
		nc_err_set(err, NC_ERR_PARAM_SEVERITY, "error");
		nc_err_set(err, NC_ERR_PARAM_MSG, "A message could not be handled because it failed to be parsed correctly.");
		break;
	}

	return (err);
}

void nc_err_free (struct nc_err* err)
{
	if (err != NULL) {
		if (err->apptag) {
			free (err->apptag);
		}
		if (err->attribute) {
			free(err->attribute);
		}
		if (err->element) {
			free(err->element);
		}
		if (err->message) {
			free(err->message);
		}
		if (err->ns) {
			free(err->ns);
		}
		if (err->path) {
			free(err->path);
		}
		if(err->severity) {
			free(err->severity);
		}
		if (err->sid) {
			free(err->sid);
		}
		if (err->tag) {
			free(err->tag);
		}
		if (err->type) {
			free(err->type);
		}
		free (err);
	}
}

char* nc_err_get(struct nc_err* err, NC_ERR_PARAM param)
{

	if (err == NULL) {
		ERROR("Invalid NETCONF error structure to set.");
		return (NULL);
	}

	switch(param) {
	case NC_ERR_PARAM_TYPE:
		return (err->type);
	case NC_ERR_PARAM_TAG:
		return (err->tag);
	case NC_ERR_PARAM_SEVERITY:
		return (err->severity);
	case NC_ERR_PARAM_APPTAG:
		return (err->apptag);
	case NC_ERR_PARAM_PATH:
		return (err->path);
	case NC_ERR_PARAM_MSG:
		return (err->message);
	case NC_ERR_PARAM_INFO_BADATTR:
		return (err->attribute);
	case NC_ERR_PARAM_INFO_BADELEM:
		return (err->element);
	case NC_ERR_PARAM_INFO_BADNS:
		return (err->ns);
	case NC_ERR_PARAM_INFO_SID:
		return (err->sid);
	default:
		ERROR("Unknown parameter for NETCONF error to get.");
		return (NULL);
	}
}

int nc_err_set (struct nc_err* err, NC_ERR_PARAM param, char* value)
{
	char** param_item = NULL;

	if (err == NULL) {
		ERROR("Invalid NETCONF error structure to set.");
		return (EXIT_FAILURE);
	}
	if (value == NULL) {
		ERROR("Invalid value for NETCONF error parameter.");
		return (EXIT_FAILURE);
	}

	/* find out which parameter will be set */
	switch (param) {
	case NC_ERR_PARAM_TYPE:
		param_item = &(err->type);
		break;
	case NC_ERR_PARAM_TAG:
		param_item = &(err->tag);
		break;
	case NC_ERR_PARAM_SEVERITY:
		param_item = &(err->severity);
		break;
	case NC_ERR_PARAM_APPTAG:
		param_item = &(err->apptag);
		break;
	case NC_ERR_PARAM_PATH:
		param_item = &(err->path);
		break;
	case NC_ERR_PARAM_MSG:
		param_item = &(err->message);
		break;
	case NC_ERR_PARAM_INFO_BADATTR:
		param_item = &(err->attribute);
		break;
	case NC_ERR_PARAM_INFO_BADELEM:
		param_item = &(err->element);
		break;
	case NC_ERR_PARAM_INFO_BADNS:
		param_item = &(err->ns);
		break;
	case NC_ERR_PARAM_INFO_SID:
		param_item = &(err->sid);
		break;
	default:
		ERROR("Unknown parameter for NETCONF error to set.");
		break;
	}

	/* set selected parameter to the specified value */
	if (param_item != NULL) {
		if (*param_item != NULL) {
			/* remove previous value if any */
			free(*param_item);
		}
		*param_item = strdup (value);
	}

	return (EXIT_SUCCESS);
}
