/**
 * \file netconf_internal.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf's internal functions and structures definitions.
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

#ifndef NETCONF_INTERNAL_H_
#define NETCONF_INTERNAL_H_

#include <libssh2.h>
#include <libxml/tree.h>

#include "netconf.h"
#include "callbacks.h"

#define SID_SIZE 	16

#define UTF8 		"UTF-8"
#define XML_VERSION 	"1.0"

/**
 * @brief NETCONF v1.0 message separator.
 * @ingroup internalAPI
 */
#define NC_V10_END_MSG      "]]>]]>"

/**
 * @brief NETCONF v1.1 message separator (part of chunked framing mechanism).
 * @ingroup internalAPI
 */
#define NC_V11_END_MSG      "\n##\n"

/**
 * @brief Default NETCONF port number assigned by IANA.
 * @ingroup internalAPI
 */
#define NC_PORT             830

/* NETCONF namespaces */
#define NC_NS_BASE10		"urn:ietf:params:xml:ns:netconf:base:1.0"
#define NC_NS_BASE10_ID		"base10"
#define NC_NS_BASE11		"urn:ietf:params:xml:ns:netconf:base:1.1"
#define NC_NS_BASE11_ID		"base11"

#define NC_NS_BASE NC_NS_BASE10
#define NC_NS_BASE_ID NC_NS_BASE10_ID

#define NC_CAP_BASE10_ID      "urn:ietf:params:netconf:base:1.0"
#define NC_CAP_BASE11_ID      "urn:ietf:params:netconf:base:1.1"
#define NC_CAP_NOTIFICATION_ID "urn:ietf:params:xml:ns:netconf:notification:1.0"
#define NC_CAP_WRUNNING_ID  "urn:ietf:params:netconf:capability:writable-running:1.0"
#define NC_CAP_CANDIDATE_ID "urn:ietf:params:netconf:capability:candidate:1.0"
#define NC_CAP_STARTUP_ID   "urn:ietf:params:netconf:capability:startup:1.0"
#define NC_CAP_POWERCTL_ID  "urn:liberouter:params:netconf:capability:power-control:1.0"
#define NC_CAP_CONFIRMED_COMMIT_ID "urn:ietf:params:netconf:capability:confirmed-commit:1.1"
#define NC_CAP_ROLLBACK_ID		"urn:ietf:params:netconf:capability:rollback-on-error:1.0"
#define NC_CAP_VALIDATE_ID		"urn:ietf:params:netconf:capability:validate:1.1"

/* NETCONF versions identificators */
#define NETCONFV10	0
#define NETCONFV11	1
#define NETCONFVUNK -1

/* RPC model elements */
#define NC_HELLO_MSG        "hello"
#define NC_RPC_MSG          "rpc"
#define NC_RPC_REPLY_MSG    "rpc-reply"
#define NC_RPC_ERROR        "rpc-error"
#define NC_RPC_OK           "ok"
#define NC_RPC_DATA         "data"


/* libnetconf's message printing */
char prv_msg[4096];
void prv_print(const char* msg);
extern int verbose_level;
#define ERROR(format,args...) if(verbose_level>=NC_VERB_ERROR){snprintf(prv_msg,4095,"ERROR: "format,##args); prv_print(prv_msg);}
#define WARN(format,args...) if(verbose_level>=NC_VERB_WARNING){snprintf(prv_msg,4095,"WARNING: "format,##args); prv_print(prv_msg);}
#define VERB(format,args...) if(verbose_level>=NC_VERB_VERBOSE){snprintf(prv_msg,4095,"VERBOSE: "format,##args); prv_print(prv_msg);}
#define DBG(format,args...) if(verbose_level>=NC_VERB_DEBUG){snprintf(prv_msg,4095,"DEBUG: "format,##args); prv_print(prv_msg);}

/**
 * @brief Callbacks structure for all callbacks functions that can be set by application
 * @ingroup internalAPI
 */
struct callbacks {
	/**< @brief Message printing function, if not set, messages are suppressed */
	int (*print)(const char* msg);
	/**< @brief Callback for libssh2's 'keyboard-interactive' authentication method */
	void (*sshauth_interactive)(const char* name,
			int name_len,
			const char* instruction,
			int instruction_len,
			int num_prompts,
			const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
			LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
			void** abstract);
	/**< @brief Callback for passing password for libssh2's 'password' authentication method */
	char* (*sshauth_password)(const char* username, const char* hostname);
	/**< @brief Callback for passing passphrase for libssh2's 'publickey' authentication method */
	char* (*sshauth_passphrase)(const char* username, const char* hostname, const char* privatekey_filepath);
	/**< @brief Callback to get answer to the host authenticity: 0 ok, 1 failed */
	int (*hostkey_check)(const char* hostname, int keytype, const char* fingerprint);
	/**< @brief */
	char *publickey_filename;
	/**< @brief */
	char *privatekey_filename;
};

/**
 * @ingroup internalAPI
 * @brief Header instance of the callbacks structure storing all callbacks set by application
 *
 * Real instance of the callbacks structure is placed in callbacks.c where
 * also default values are set.
 */
extern struct callbacks callbacks;

/**
 * @ingroup internalAPI
 * @brief NETCONF session description structure
 *
 * No one outside the libnetconf would access members of this structure.
 */
struct nc_session {
	/**< @brief Session ID */
	char session_id[SID_SIZE];
	/**< @brief Last message ID */
	nc_msgid msgid;
	/**< @brief only for clients using libssh2 for communication */
	int libssh2_socket;
	/**< @brief Input file descriptor for communication with (reading from) the other side of the NETCONF session */
	int fd_input;
	/**< @brief Output file descriptor for communication with (writing to) the other side of the NETCONF session */
	int fd_output;
	/**< @brief */
	LIBSSH2_SESSION * ssh_session;
	/**< @brief */
	LIBSSH2_CHANNEL * ssh_channel;
	/**< @brief netopeer-agent's hostname */
	char *hostname;
	/**< @brief netopeer-agent's port */
	char *port;
	/**< @brief name of the user holding the session */
	char *username;
	/**< @brief number of configrmed capabilities */
	struct nc_cpblts *capabilities;
	/**< @brief NETCONF protocol version */
	int version;
};

/* define error elements */
#define NC_ERR_TAG_IN_USE "in-use"
#define NC_ERR_TAG_INVALID_VALUE "invalid-value"
#define NC_ERR_TAG_TOO_BIG "too-big"
#define NC_ERR_TAG_MISSING_ATTR "missing-attribute"
#define NC_ERR_TAG_BAD_ATTR "bad-attribute"
#define NC_ERR_TAG_UNKN_ATTR "unknown-attribute"
#define NC_ERR_TAG_MISSING_ELEM "missing-element"
#define NC_ERR_TAG_BAD_ELEM "bad-element"
#define NC_ERR_TAG_UNKN_ELEM "unknown-element"
#define NC_ERR_TAG_UNKN_NAMESPACE "unknown-namespace"
#define NC_ERR_TAG_ACCESS_DENIED "access-denied"
#define NC_ERR_TAG_LOCK_DENIED "lock-denied"
#define NC_ERR_TAG_RES_DENIED "resource-denied"
#define NC_ERR_TAG_ROLLBCK "rollback-failed"
#define NC_ERR_TAG_DATA_EXISTS "data-exists"
#define NC_ERR_TAG_DATA_MISSING "data-missing"
#define NC_ERR_TAG_OP_NOT_SUPPORTED "operation-not-supported"
#define NC_ERR_TAG_OP_FAILED "operation-failed"
#define NC_ERR_TAG_PARTIAL_OP "partial-operation"
#define NC_ERR_TAG_MALFORMED_MSG "malformed-message"

#define NC_ERR_TYPE_RPC     "rpc"
#define NC_ERR_TYPE_PROT    "protocol"
#define NC_ERR_TYPE_APP     "application"
#define NC_ERR_TYPE_TRANS   "transport"

#define NC_ERR_SEV_ERR      "error"
#define NC_ERR_SEV_WARN     "warning"

/**
 * @brief NETCONF error structure representation
 * @ingroup internalAPI
 */
struct nc_err {
	/**
	 * @brief Mask of #nc_err's fields to be freed.
	 *
	 * If this field is not filled with the rest of fields, no #nc_err's
	 * field is freed. Value is result of bitwise OR operation on
	 * NC_ERR_FREE_* values.
	 */
	int free_mask;
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
        /* partial operation info */
        /**
         * @brief noop-element identifies an element in the data model for which
         *        the requested operation was not attempted for that node and
         *        all its child nodes. This element can appear zero or more
         *        times.
         *
         * This information is part of error-info element if error-tag is set to #NC_ERR_TAG_PARTIAL_OP.
         */
        char *noop_elem;
        /**
         * @brief ok-element identifies an element in the data model for which
         *        the requested operation has been completed for that node and
         *        all its child nodes. This element can appear zero or more
         *        times.
         *
         * This information is part of error-info element if error-tag is set to #NC_ERR_TAG_PARTIAL_OP.
         */
        char *ok_elem;    /**< ok-element - dynamic string, is freed with nc_error_free */
        /**
         * @brief err-element identifies an element in the data model for which
         *        the requested operation has failed for that node and all its
         *        child nodes. This element can appear zero or more times.
         *
         * This information is part of error-info element if error-tag is set to #NC_ERR_TAG_PARTIAL_OP.
         */
        char *err_elem;   /**< err-element - dynamic string, is freed with nc_error_free */
};

/**
 * @brief generic message structure covering both rpc and reply.
 * @ingroup internalAPI
 */
struct nc_msg {
	xmlDocPtr doc;
	nc_msgid msgid;
	NC_REPLY_TYPE type;
};

struct nc_filter {
	NC_FILTER_TYPE type;
	char *type_string;
	char *content;
};

struct nc_cpblts {
	int iter;
	int list_size;
	int items;
	char **list;
};

/**
 * @brief Get copy of the given string without whitespaces.
 *
 * @param[in] in string to clear.
 *
 * return Copy of the given string without whitespaces. Caller is supposed to free it.
 */
char* nc_clrwspace (const char* in);

#endif /* NETCONF_INTERNAL_H_ */
