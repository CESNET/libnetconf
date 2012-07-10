/**
 * \file netconf.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf's general public functions and structures definitions.
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

#ifndef NETCONF_H_
#define NETCONF_H_

struct nc_msg;

/**
 * @brief rpc message.
 * @ingroup rpc
 */
typedef struct nc_msg nc_rpc;

/**
 * @brief reply message.
 * @ingroup reply
 */
typedef struct nc_msg nc_reply;

/**
 * @ingroup session
 * @brief NETCONF capabilities structure
 */
struct nc_cpblts;

/**
 * @ingroup session
 * @brief Type representing NETCONF message-id attribute.
 */
typedef long long unsigned int nc_msgid;

/**
 * @brief NETCONF session description structure
 * @ingroup session
 */
struct nc_session;

/**
 * @ingroup rpc
 * @brief NETCONF filter.
 */
struct nc_filter;

/**
 * @ingroup session
 * @brief Enumeration of possible states of the NETCONF session.
 */
typedef enum {
	NC_SESSION_STATUS_ERROR = -1, /**< undefined status or error return code */
	NC_SESSION_STATUS_STARTUP = 0, /**< session is setting up */
	NC_SESSION_STATUS_WORKING = 1, /**< session is established and ready to work */
	NC_SESSION_STATUS_CLOSED = 2 /**< session was closed and could not be used to communication */
} NC_SESSION_STATUS;

/**
 * @brief Enumeration of \<rpc-reply\> types.
 * @ingroup reply
 */
typedef enum NC_REPLY_TYPE {
	NC_REPLY_UNKNOWN, /**< value describing that no rpc-reply type was detected so far */
	NC_REPLY_HELLO, /**< \<hello\> message type, same as NC_RPC_HELLO */
	NC_REPLY_OK, /**< \<ok\> rpc-reply message type */
	NC_REPLY_ERROR, /**< \<rpc-error\> rpc-reply message type */
	NC_REPLY_DATA /**< rpc-reply message containing \<data\> */
} NC_REPLY_TYPE;

/**
 * @brief Enumeration of \<rpc\> operation types.
 * @ingroup rpc
 */
typedef enum NC_RPC_TYPE {
	NC_RPC_UNKNOWN, /**< value describing that no supported operation type was detected so far */
	NC_RPC_HELLO, /**< \<hello\> message type, same as NC_REPLY_HELLO */
	NC_RPC_DATASTORE, /**< \<rpc\> contains operation affecting datastore */
	NC_RPC_SESSION, /**< \<rpc\> contains operation affecting session */
} NC_RPC_TYPE;

/**
 * @brief Enumeration of supported \<rpc\> operations
 * @ingroup rpc
 */
typedef enum NC_OP {
	NC_OP_UNKNOWN,		/**< unknown/error value */
	NC_OP_GETCONFIG,	/**< \<get-config\> operation */
	NC_OP_GET,		/**< \<get\> operation */
	NC_OP_EDITCONFIG,	/**< \<edit-config\> operation */
	NC_OP_CLOSESESSION,	/**< \<close-session\> operation */
	NC_OP_KILLSESSION,	/**< \<kill-session\> operation */
	NC_OP_COPYCONFIG,	/**< \<copy-config\> operation */
	NC_OP_DELETECONFIG,	/**< \<delete-config\> operation */
	NC_OP_LOCK,		/**< \<lock\> operation */
	NC_OP_UNLOCK		/**< \<unlock\> operation */
} NC_OP;

typedef enum {
	/**
	 * error-type - The conceptual layer that the error occurred, accepted
	 * values include 'transport', 'rpc', 'protocol', 'application'.
	 */
	NC_ERR_PARAM_TYPE,
	/**
	 * error-tag - Contains a string identifying the error condition.
	 */
	NC_ERR_PARAM_TAG,
	/**
	 * error-severity - The error severity, accepted values are 'error' and
	 * 'warning'.
	 */
	NC_ERR_PARAM_SEVERITY,
	/**
	 * error-app-tag - Contains a string identifying the data-model-specific
	 * or implementation-specific error condition, if one exists.  This
	 * element will not be present if no appropriate application error-tag
	 * can be associated with a particular error condition.  If a
	 * data-model-specific and an implementation-specific error-app-tag
	 * both exist, then the data-model-specific value MUST be used by the
	 * server.
	 */
	NC_ERR_PARAM_APPTAG,
	/**
	 * error-path - Contains the absolute XPath expression identifying the
	 * element path to the node that is associated with the error being
	 * reported.
	 */
	NC_ERR_PARAM_PATH,
	/**
	 * error-message - A string describing the error.
	 */
	NC_ERR_PARAM_MSG,
	/**
	 * bad-attribute in error-info - name of the attribute, contained in
	 * 'bad-attribute', 'missing-attribute' and 'unknown-attribute' errors.
	 */
	NC_ERR_PARAM_INFO_BADATTR,
	/**
	 * bad-element in error-info - name of the element, contained in
	 * 'missing-attribute', bad-attribute', 'unknown-attribute',
	 * 'missing-element', 'bad-element', 'unknown-element' and
	 * 'unknown-namespace' errors.
	 */
	NC_ERR_PARAM_INFO_BADELEM,
	/**
	 * bad-namespace in error-info - name of the unexpected namespace,
	 * contained in 'unknown-namespace' error.
	 */
	NC_ERR_PARAM_INFO_BADNS,
	/**
	 * session-id in error-info - session ID of session holding the
	 * requested lock, contained in 'lock-denied' error.
	 */
	NC_ERR_PARAM_INFO_SID,
} NC_ERR_PARAM;

/**
 * @brief Enumeration of supported types of datastores defined by NETCONF
 * @ingroup store
 */
typedef enum NC_DATASTORE_TYPE {
	NC_DATASTORE_NONE, /**< value describing that no datastore was selected, (usage similar to NULL) */
	NC_DATASTORE_RUNNING, /**< base NETCONF's datastore containing current device configuration */
	NC_DATASTORE_STARTUP, /**< separated startup datastore as defined in Distinct Startup Capability */
	NC_DATASTORE_CANDIDATE /**< separated working datastore as defined in Candidate Configuration Capability */
} NC_DATASTORE_TYPE;

/**
 * @ingroup rpc
 * @brief Enumeration of supported NETCONF filter types.
 */
typedef enum NC_FILTER_TYPE {
	NC_FILTER_SUBTREE//!< NC_FILTER_SUBTREE
} NC_FILTER_TYPE;

/* default operations IDs for edit-config */
typedef enum NC_EDIT_OP_TYPE {
	NC_EDIT_OP_ERROR = -1, /* for internal purposes, not defined by NETCONF */
	/* NC_EDIT_OP_TYPE_NONE for compatibility with NC_DEFOP_TYPE we start with value 1 */
	NC_EDIT_OP_MERGE = 1,
	NC_EDIT_OP_REPLACE = 2,
	NC_EDIT_OP_CREATE,
	NC_EDIT_OP_DELETE,
	NC_EDIT_OP_REMOVE
} NC_EDIT_OP_TYPE;

typedef enum NC_EDIT_DEFOP_TYPE {
	NC_EDIT_DEFOP_NONE = 1,
	NC_EDIT_DEFOP_MERGE = 2,
	NC_EDIT_DEFOP_REPLACE = 3
} NC_EDIT_DEFOP_TYPE;

typedef enum NC_EEDIT_RROPT_TYPE {
	NC_EDIT_ERROPT_STOP = 1,
	NC_EDIT_ERROPT_CONT = 2,
	NC_EDIT_ERROPT_ROLLBACK = 3
} NC_EDIT_ERROPT_TYPE;

/**
 * @brief Verbosity levels.
 * @ingroup genAPI
 */
typedef enum {
	NC_VERB_ERROR,  //!< Print only error messages.
	NC_VERB_WARNING,//!< Print error and warning messages.
	NC_VERB_VERBOSE,//!< Besides errors and warnings, print some other verbose messages.
	NC_VERB_DEBUG   //!< Print all messages including some development debug messages.
} NC_VERB_LEVEL;

/**
 * @brief Set libnetconf's verbosity level.
 * @param[in] level Enabled verbosity level (includes all levels with higher priority).
 * @ingroup genAPI
 */
void nc_verbosity(NC_VERB_LEVEL level);

#endif /* NETCONF_H_ */
