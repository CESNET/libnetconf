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

#include <time.h>
#include <pthread.h>

#ifndef DISABLE_LIBSSH
#	include <libssh2.h>
#endif

#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "netconf.h"
#include "callbacks.h"
#include "with_defaults.h"

#define SID_SIZE 	16

#define UTF8 		"UTF-8"
#define XML_VERSION 	"1.0"

/**
 * @brief NETCONF v1.0 message separator.
 * @ingroup internalAPI
 */
#define NC_V10_END_MSG      "]]>]]>"

/**
 * @brief NETCONF v1.1 message separator (part of the chunked framing mechanism).
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

#define NC_NS_YIN 		"urn:ietf:params:xml:ns:yang:yin:1"

#define NC_NS_BASE NC_NS_BASE10
#define NC_NS_BASE_ID NC_NS_BASE10_ID

#define NC_CAP_BASE10_ID      	"urn:ietf:params:netconf:base:1.0"
#define NC_CAP_BASE11_ID      	"urn:ietf:params:netconf:base:1.1"
#define NC_CAP_NOTIFICATION_ID 	"urn:ietf:params:netconf:capability:notification:1.0"
#define NC_CAP_INTERLEAVE_ID 	"urn:ietf:params:netconf:capability:interleave:1.0"
#define NC_CAP_WRUNNING_ID  	"urn:ietf:params:netconf:capability:writable-running:1.0"
#define NC_CAP_CANDIDATE_ID 	"urn:ietf:params:netconf:capability:candidate:1.0"
#define NC_CAP_STARTUP_ID   	"urn:ietf:params:netconf:capability:startup:1.0"
#define NC_CAP_POWERCTL_ID 	"urn:liberouter:params:netconf:capability:power-control:1.0"
#define NC_CAP_CONFIRMED_COMMIT_ID "urn:ietf:params:netconf:capability:confirmed-commit:1.1"
#define NC_CAP_ROLLBACK_ID	"urn:ietf:params:netconf:capability:rollback-on-error:1.0"
#define NC_CAP_VALIDATE10_ID	"urn:ietf:params:netconf:capability:validate:1.0"
#define NC_CAP_VALIDATE11_ID	"urn:ietf:params:netconf:capability:validate:1.1"
#define NC_CAP_MONITORING_ID "urn:ietf:params:xml:ns:yang:ietf-netconf-monitoring"
#define NC_CAP_WITHDEFAULTS_ID 	"urn:ietf:params:netconf:capability:with-defaults:1.0"

#define NC_NS_WITHDEFAULTS 	"urn:ietf:params:xml:ns:yang:ietf-netconf-with-defaults"
#define NC_NS_WITHDEFAULTS_ID   "wd"
#define NC_NS_NOTIFICATIONS "urn:ietf:params:xml:ns:netconf:notification:1.0"
#define NC_NS_NOTIFICATIONS_ID 	"ntf"
#define NC_NS_MONITORING 	"urn:ietf:params:xml:ns:yang:ietf-netconf-monitoring"
#define NC_NS_MONITORING_ID 	"monitor"

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

#define SSH2_KEYS 3 /* the number of supported keys */

/*
 * Special session ID to be used by libnetconf's internal dummy sessions. This
 * kind of dummy sessions does not break datastore locks on a session close.
 */
#define INTERNAL_DUMMY_ID "0"

/*
 * how to send NETCONF XML content:
 * 1 - formatted, i.e. with new lines and spaces
 * 0 - unformatted, only the content without any unnecessary white space formatting characters
 */
#define NC_CONTENT_FORMATTED 1

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/*
 * libnetconf permissions for every file or dir creation, process mask used with special cases
 * such as fopen()
 */

/* safe permissions - only reading and writing performed by the owner allowed */
#define FILE_PERM 0600
#define DIR_PERM 0700
#define MASK_PERM 0066

/* libnetconf's message printing */
char prv_msg[4096];
void prv_print(NC_VERB_LEVEL level, const char* msg);
extern int verbose_level;
#define ERROR(format,args...) if(verbose_level>=NC_VERB_ERROR){snprintf(prv_msg,4095,format,##args); prv_print(NC_VERB_ERROR, prv_msg);}
#define WARN(format,args...) if(verbose_level>=NC_VERB_WARNING){snprintf(prv_msg,4095,format,##args); prv_print(NC_VERB_WARNING, prv_msg);}
#define VERB(format,args...) if(verbose_level>=NC_VERB_VERBOSE){snprintf(prv_msg,4095,format,##args); prv_print(NC_VERB_VERBOSE, prv_msg);}
#define DBG(format,args...) if(verbose_level>=NC_VERB_DEBUG){snprintf(prv_msg,4095,format,##args); prv_print(NC_VERB_DEBUG, prv_msg);}
#ifdef DEBUG_THREADS
#define DBG_UNLOCK(name) DBG("Unlocking %s in thread %lu (%s:%d)", name, pthread_self(), __FILE__, __LINE__)
#define DBG_LOCK(name) DBG("Locking %s in thread %lu (%s:%d)", name, pthread_self(), __FILE__, __LINE__)
#else
#define DBG_UNLOCK(name)
#define DBG_LOCK(name)
#endif

/**
 * @brief Callbacks structure for all the callback functions that can be set by an application
 * @ingroup internalAPI
 */
struct callbacks {
	/**< @brief Message printing function, if not set, all the messages are suppressed */
	void (*print)(NC_VERB_LEVEL level, const char* msg);
	/**< @brief Function processing \<rpc-error\> replies on the client side. If no callback function is set, the error details are ignored */
	void (*process_error_reply)(const char* tag,
			const char* type,
			const char* severity,
			const char* apptag,
			const char* path,
			const char* message,
			const char* attribute,
			const char* element,
			const char* ns,
			const char* sid);
#ifndef DISABLE_LIBSSH
	/**< @brief Callback for libssh2's 'keyboard-interactive' authentication method */
	void (*sshauth_interactive)(const char* name,
			int name_len,
			const char* instruction,
			int instruction_len,
			int num_prompts,
			const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
			LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
			void** abstract);
	/**< @brief Callback for passing the password for libssh2's 'password' authentication method */
	char* (*sshauth_password)(const char* username, const char* hostname);
	/**< @brief Callback for passing the passphrase for libssh2's 'publickey' authentication method */
	char* (*sshauth_passphrase)(const char* username, const char* hostname, const char* privatekey_filepath);
	/**< @brief Callback to check the host authenticity: 0 ok, 1 failed */
	int (*hostkey_check)(const char* hostname, int keytype, const char* fingerprint);
	/**< @brief */
	char *publickey_filename[SSH2_KEYS];
	/**< @brief */
	char *privatekey_filename[SSH2_KEYS];
	/**< @brief is private key protected by password */
	int key_protected[SSH2_KEYS];
#endif
};

/**
 * @ingroup internalAPI
 * @brief Header instance of a callback structure storing all the callbacks set by the application
 *
 * Real instance of the callbacks structure is placed in callbacks.c where
 * the default values are also set.
 */
extern struct callbacks callbacks;

/**
 * @ingroup internalAPI
 * @brief NETCONF session statistics as defined in RFC 6022 (as common-counters)
 */
struct nc_session_stats {
	unsigned int in_rpcs;
	unsigned int in_bad_rpcs;
	unsigned int out_rpc_errors;
	unsigned int out_notifications;
};

/**
 * @ingroup internalAPI
 * @brief NETCONF statistics section as defined in RFC 6022
 */
struct nc_statistics {
	unsigned int participants;
#define TIME_LENGTH 21
	char start_time[TIME_LENGTH];
	unsigned int bad_hellos;
	unsigned int sessions_in;
	unsigned int sessions_dropped;
	struct nc_session_stats counters;
};

/**
 * @ingroup internalAPI
 * @brief Information structure shared between all libnetconf's processes.
 */
struct nc_shared_info {
	pthread_rwlock_t lock;
	struct nc_statistics stats;
};

/**
 * @ingroup internalAPI
 * @brief NETCONF session description structure
 *
 * No one outside libnetconf can access members of this structure.
 */
struct nc_session {
	/**< @brief Session ID */
	char session_id[SID_SIZE];
	/**< @brief Last message ID */
	long long unsigned int msgid;
	/**< @brief only for clients using libssh2 for communication */
	int libssh2_socket;
	/**< @brief Input file descriptor for communication with (reading from) the other side of the NETCONF session */
	int fd_input;
#ifdef DISABLE_LIBSSH
	/**< @brief FILE structure for the fd_input file descriptor. This is used only if libssh2 is not used */
	FILE *f_input;
#endif
	/**< @brief Output file descriptor for communication with (writing to) the other side of the NETCONF session */
	int fd_output;
#ifndef DISABLE_LIBSSH
	/**< @brief */
	LIBSSH2_SESSION * ssh_session;
	/**< @brief */
	LIBSSH2_CHANNEL * ssh_channel;
#else
	void *ssh_session;
	void *ssh_channel;
#endif
	/**< @brief netopeer-agent's hostname */
	char *hostname;
	/**< @brief netopeer-agent's port */
	char *port;
	/**< @brief name of the user holding the session */
	char *username;
	/**< @brief login time in the yang:date-and-time format */
	char *logintime;
	/**< @brief number of confirmed capabilities */
	struct nc_cpblts *capabilities;
	/**< @brief serialized original capabilities of a server/client */
	char *capabilities_original;
	/**< @brief NETCONF protocol version */
	int version;
	/**< @brief session's with-defaults basic mode */
	NCWD_MODE wd_basic;
	/**< @brief session's with-defaults ORed supported modes */
	int wd_modes;
	/**< @brief status of the NETCONF session */
	NC_SESSION_STATUS status;
	/**< @brief thread lock for accessing session items */
	pthread_mutex_t mut_session;
	/**< @brief thread lock for accessing output */
	pthread_mutex_t mut_out;
	/**< @brief thread lock for accessing in */
	pthread_mutex_t mut_in;
	/**< @brief thread lock for accessing queue_event */
	pthread_mutex_t mut_equeue;
	/**< @brief thread lock for accessing queue_msg */
	pthread_mutex_t mut_mqueue;
	/**< @brief queue for received, but not processed, NETCONF messages */
	struct nc_msg* queue_msg;
	/**< @brief queue for received, but not processed, NETCONF Event Notifications */
	struct nc_msg* queue_event;
	/**< @brief flag for active notification subscription on the session */
	int ntf_active;
	/**< @brief Flag if the session is monitored and connected to the shared memory segment */
	int monitored;
	/**< @brief NETCONF session statistics as defined in RFC 6022 */
	struct nc_session_stats *stats;
};

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
	 * @brief XPATH expression identifying the element with the error.
	 */
	char *path;
	/**
	 * @brief Human-readable description of the error.
	 */
	char *message;
	/**
	 * @brief Name of the data-model-specific XML attribute that caused the error.
	 * \todo: The model defines this as a sequence, so we have to support storing of multiple values for this
	 * This information is a part of the error-info element.
	 */
	char *attribute;
	/**
	 * @brief Name of the data-model-specific XML element that caused the error.
	 * \todo: The model defines this as a sequence, so we have to support storing of multiple values for this
	 * This information is a part of the error-info element.
	 */
	char *element;
	/**
	 * @brief Name of the unexpected XML namespace that caused the error.
	 * \todo: The model defines this as a sequence, so we have to support storing of multiple values for this
	 * This information is a part of the error-info element.
	 */
	char *ns;
	/**
	 * @brief Session ID of the session holding the requested lock.
	 *
	 * This information is a part of the error-info element.
	 */
	char *sid;
	/**
	 * @brief Pointer to the next error in the list
	 */
	struct nc_err* next;
};

/**
 * @brief generic message structure covering both a rpc and a reply.
 * @ingroup internalAPI
 */
struct nc_msg {
	xmlDocPtr doc;
	xmlXPathContextPtr ctxt;
	char* msgid;
	union {
		NC_REPLY_TYPE reply;
		NC_RPC_TYPE rpc;
		NC_NOTIF_TYPE ntf;
	} type;
	NCWD_MODE with_defaults;
	struct nc_err* error;
	struct nc_msg* next;
};

struct nc_filter {
	NC_FILTER_TYPE type;
	xmlNodePtr subtree_filter;
};

struct nc_cpblts {
	int iter;
	int list_size;
	int items;
	char **list;
};

/**
 * @brief Get a copy of the given string without whitespaces.
 *
 * @param[in] in string to clear.
 *
 * return Copy of the given string without whitespaces. The caller is supposed to free it.
 */
char* nc_clrwspace (const char* in);

/**
 * @brief Process config data according to with-defaults' mode and data model
 * @param[in] config XML configuration data document in which the default values will
 * be modified (added for report-all and removed for trim mode).
 * @param[in] model Configuration data model for the data given in the config parameter.
 * @param[in] mode With-defaults capability mode for the configuration data modification.
 * @return 0 on success, non-zero else.
 */
int ncdflt_default_values(xmlDocPtr config, const xmlDocPtr model, NCWD_MODE mode);

/**
 * @breaf Remove the defaults nodes from copy-config and edit-config config.
 *
 * This function should be used only if report-all-tagged mode is supported.
 *
 * @param[in] config XML configuration data document in which the default node will
 * be checked if it contains 'default' attribute and also the correct default value
 * according to the model and in such case the node will be returned to its
 * default value (i.e. removed from the config where it can be again added by
 * a following input operation).
 * @param[in] model Configuration data model for the data given in the config parameter.
 * @return 0 on success, non-zero else.
 */
int ncdflt_default_clear(xmlDocPtr config, const xmlDocPtr model);

/**
 * @ingroup internalAPI
 * @brief Transform given time_t (seconds since the epoch) into the RFC 3339 format
 * accepted by NETCONF functions.
 *
 * This is a reverse function to nc_datetime2time().
 *
 * @param[in] time time_t type value returned e.g. by time().
 * @return Printed string in a format compliant to RFC 3339. It is up to the
 * caller to free the returned string.
 */
char* nc_time2datetime(time_t time);

/**
 * @ingroup internalAPI
 * @brief Transform given string in RFC 3339 compliant format to the time_t
 * (seconds since the epoch) accepted by most Linux functions.
 *
 * This is a reverse function to nc_time2datetime().
 *
 * @param[in] time Time structure returned e.g. by localtime().
 * @return time_t value of the given string.
 */
time_t nc_datetime2time(const char* datetime);

/**
 * @brief Parse the given reply message and create a NETCONF error structure
 * describing the error from the reply. The reply must be of #NC_REPLY_ERROR type.
 * @param[in] reply \<rpc-reply\> message to be parsed.
 * @return Filled error structure according to given rpc-reply, returned value
 * is automatically connected with the given rpc-reply and should not be freed
 * separately. This behaviour should be changed when this function will be a part
 * of the public API and used by applications.
 */
struct nc_err* nc_err_parse(nc_reply* reply);

/**
 * @brief Apply filter on the given XML document.
 * @param data XML document to be filtered.
 * @param filter Filter to apply. Only 'subtree' filters are supported.
 * @return 0 on success,\n non-zero else
 */
int ncxml_filter(xmlNodePtr old, const struct nc_filter * filter, xmlNodePtr *new);

/**
 * @brief Get state information about sessions. Only information about monitored
 * sessions added by nc_session_monitor() is provided.
 * @return Serialized XML describing session as defined in RFC 6022, NULL on
 * error (or if no session is monitored).
 */
char* nc_session_stats(void);

#ifndef DISABLE_NOTIFICATIONS

/**
 * @brief Initiate the NETCONF Notifications environment
 * @return 0 on success, non-zero value else
 */
int ncntf_init(void);

/**
 * @brief Close all the NETCONF Event Streams and other parts of the Notification
 * environment.
 */
void ncntf_close(void);

#endif /* DISABLE_NOTIFICATIONS */

int nc_session_monitoring_init(void);
void nc_session_monitoring_close(void);

/**
 * @brief Remove namespace definitions from the node which are no longer used.
 * @param[in] node XML element node which is to be checked for namespace definitions
 */
void nc_clear_namespaces(xmlNodePtr node);

#endif /* NETCONF_INTERNAL_H_ */
