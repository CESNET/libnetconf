/**
 * \file messages.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to create NETCONF messages.
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

#define _GNU_SOURCE
#define _BSD_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "messages.h"
#include "netconf_internal.h"
#include "error.h"
#include "messages_internal.h"
#include "with_defaults.h"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";


static struct nc_filter *nc_filter_new_subtree(const xmlNodePtr filter)
{
	struct nc_filter *retval;

	retval = malloc(sizeof(struct nc_filter));
	if (retval == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	retval->type = NC_FILTER_SUBTREE;
	retval->subtree_filter = xmlNewNode(NULL, BAD_CAST "filter");
	if (retval->subtree_filter == NULL) {
		ERROR("xmlNewNode failed (%s:%d).", __FILE__, __LINE__);
		nc_filter_free(retval);
		return (NULL);
	}

	if (filter != NULL) {
		if (xmlAddChildList(retval->subtree_filter, xmlCopyNodeList(filter)) == NULL) {
			ERROR("xmlAddChildList failed (%s:%d).", __FILE__, __LINE__);
			nc_filter_free(retval);
			return (NULL);
		}
	} /* else Empty filter as defined in RFC 6241 sec. 6.4.2 is returned */

	return (retval);
}

struct nc_filter *nc_filter_new(NC_FILTER_TYPE type, ...)
{
	struct nc_filter *retval;
	char* filter_s = NULL;
	const char* arg;
	xmlDocPtr filter;
	va_list argp;

	/* init variadic arguments list */
	va_start(argp, type);

	switch (type) {
	case NC_FILTER_SUBTREE:
		/* convert string representation into libxml2 structure */
		arg = va_arg(argp, const char*);
		if (asprintf(&filter_s, "<filter type=\"subtree\">%s</filter>", (arg == NULL) ? "" : arg) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			va_end(argp);
			return (NULL);
		}
		filter = xmlReadDoc(BAD_CAST filter_s, NULL, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NSCLEAN);
		free(filter_s);
		if (filter == NULL) {
			ERROR("xmlReadDoc() failed (%s:%d).", __FILE__, __LINE__);
			va_end(argp);
			return (NULL);
		}
		retval = nc_filter_new_subtree(filter->children->children);
		xmlFreeDoc(filter);
		break;
	default:
		ERROR("%s: Invalid filter type specified.", __func__);
		va_end(argp);
		return (NULL);
	}

	va_end(argp);
	return (retval);
}

struct nc_filter *ncxml_filter_new(NC_FILTER_TYPE type, ...)
{
	struct nc_filter *retval;
	xmlNodePtr filter;
	va_list argp;

	/* init variadic arguments list */
	va_start(argp, type);

	switch (type) {
	case NC_FILTER_SUBTREE:
		filter = va_arg(argp, const xmlNodePtr);
		retval = nc_filter_new_subtree(filter);
		break;
	default:
		ERROR("%s: Invalid filter type specified.", __func__);
		va_end(argp);
		return (NULL);
	}

	va_end(argp);

	return (retval);
}

void nc_filter_free(struct nc_filter *filter)
{
	if (filter != NULL) {
		if (filter->subtree_filter) {
			xmlFreeNode(filter->subtree_filter);
		}
		free(filter);
	}
}

static char* nc_msg_dump(const struct nc_msg *msg)
{
	xmlChar *buf;
	int len;

	if (msg == NULL || msg == ((void *) -1) || msg->doc == NULL) {
		ERROR("%s: invalid input parameter.", __func__);
		return (NULL);
	}

	xmlDocDumpFormatMemory(msg->doc, &buf, &len, 1);
	return ((char*) buf);
}

char* nc_reply_dump(const nc_reply *reply)
{
	return (nc_msg_dump((struct nc_msg*)reply));
}

xmlDocPtr ncxml_reply_dump(const nc_reply *reply)
{
	if (reply == NULL || reply == ((void *) -1) || reply->doc == NULL) {
		ERROR("%s: invalid input parameter.", __func__);
		return (NULL);
	}

	return (xmlCopyDoc(reply->doc, 1));
}

char* nc_rpc_dump(const nc_rpc *rpc)
{
	return (nc_msg_dump((struct nc_msg*)rpc));
}

xmlDocPtr ncxml_rpc_dump(const nc_rpc *rpc)
{
	return (xmlCopyDoc(rpc->doc, 1));
}

static struct nc_msg* nc_msg_build (const char * msg_dump)
{
	struct nc_msg * msg;
	const char* id;

	if ((msg = calloc (1, sizeof(struct nc_msg))) == NULL) {
		return NULL;
	}

	if ((msg->doc = xmlReadMemory (msg_dump, strlen(msg_dump), NULL, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN)) == NULL) {
		free (msg);
		return NULL;
	}

	/* create xpath evaluation context */
	if ((msg->ctxt = xmlXPathNewContext(msg->doc)) == NULL) {
		ERROR("%s: rpc message XPath context can not be created.", __func__);
		nc_msg_free(msg);
		return NULL;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(msg->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(msg);
		return NULL;
	}

	if ((id = nc_msg_parse_msgid (msg)) != NULL) {
		msg->msgid = strdup(id);
	} else {
		msg->msgid = NULL;
	}
	msg->error = NULL;
	msg->with_defaults = NCWD_MODE_NOTSET;

	return msg;
}

static struct nc_msg* ncxml_msg_build(xmlDocPtr msg_dump)
{
	struct nc_msg* msg;
	const char* id;

	if ((msg = malloc(sizeof(struct nc_msg))) == NULL) {
		return NULL;
	}

	msg->doc = msg_dump;
	msg->next = NULL;
	msg->error = NULL;
	msg->with_defaults = NCWD_MODE_NOTSET;
	msg->type.rpc = 0;

	if ((id = nc_msg_parse_msgid (msg)) != NULL) {
		msg->msgid = strdup(id);
	} else {
		msg->msgid = NULL;
	}

	/* create xpath evaluation context */
	if ((msg->ctxt = xmlXPathNewContext(msg->doc)) == NULL) {
		ERROR("%s: rpc message XPath context can not be created.", __func__);
		nc_msg_free(msg);
		return NULL;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(msg->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(msg);
		return NULL;
	}

	return (msg);
}

NCWD_MODE nc_rpc_parse_withdefaults(nc_rpc* rpc, const struct nc_session *session)
{
	xmlXPathContextPtr rpc_ctxt = NULL;
	xmlXPathObjectPtr result = NULL;
	xmlChar* data;
	NCWD_MODE retval;

	if (rpc == NULL || nc_rpc_get_type(rpc) == NC_RPC_HELLO) {
		return (NCWD_MODE_NOTSET);
	}

	if (rpc->with_defaults != NCWD_MODE_NOTSET) {
		/* already known */
		return (rpc->with_defaults);
	}

	/* create xpath evaluation context */
	if ((rpc_ctxt = xmlXPathNewContext(rpc->doc)) == NULL) {
		WARN("%s: Creating XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (NCWD_MODE_NOTSET);
	}
	if (xmlXPathRegisterNs(rpc_ctxt, BAD_CAST "wd", BAD_CAST NC_NS_WITHDEFAULTS) != 0) {
		ERROR("Registering with-defaults capability namespace for the xpath context failed.");
		xmlXPathFreeContext(rpc_ctxt);
		return (NCWD_MODE_NOTSET);
	}

	/* set with-defaults if any */
	result = xmlXPathEvalExpression(BAD_CAST "//wd:with-defaults", rpc_ctxt);
	if (result != NULL) {
		switch(result->nodesetval->nodeNr) {
		case 0:
			/* set basic mode */
			if (session != NULL) {
				retval = session->wd_basic;
			} else {
				retval = NCWD_MODE_NOTSET;
			}
			break;
		case 1:
			data = xmlNodeGetContent(result->nodesetval->nodeTab[0]);
			if (xmlStrcmp(data, BAD_CAST "report-all") == 0) {
				retval = NCWD_MODE_ALL;
			} else if (xmlStrcmp(data, BAD_CAST "report-all-tagged") == 0) {
				retval = NCWD_MODE_ALL_TAGGED;
			} else if (xmlStrcmp(data, BAD_CAST "trim") == 0) {
				retval = NCWD_MODE_TRIM;
			} else if (xmlStrcmp(data, BAD_CAST "explicit") == 0) {
				retval = NCWD_MODE_EXPLICIT;
			} else {
				WARN("%s: unknown with-defaults mode detected (%s), disabling with-defaults.", __func__, data);
				retval = NCWD_MODE_NOTSET;
			}
			xmlFree(data);
			break;
		default:
			retval = NCWD_MODE_NOTSET;
			break;
		}
		xmlXPathFreeObject(result);
	} else {
		/* set basic mode */
		retval = ncdflt_get_basic_mode();
	}
	xmlXPathFreeContext(rpc_ctxt);

	rpc->with_defaults = retval;
	return (retval);
}

nc_rpc * nc_rpc_build (const char * rpc_dump)
{
	nc_rpc* rpc;

	if ((rpc = nc_msg_build (rpc_dump)) == NULL) {
		return NULL;
	}
	/* set rpc type flag */
	nc_rpc_get_type(rpc);

	/* set with-defaults if any */
	nc_rpc_parse_withdefaults(rpc, NULL);

	return rpc;
}

nc_rpc* ncxml_rpc_build(xmlDocPtr rpc_dump)
{
	nc_rpc* rpc;

	if ((rpc = ncxml_msg_build (rpc_dump)) == NULL) {
		return NULL;
	}

	/* set rpc type flag */
	nc_rpc_get_type(rpc);

	/* set with-defaults if any */
	nc_rpc_parse_withdefaults(rpc, NULL);

	return rpc;
}

nc_reply * nc_reply_build (const char* reply_dump)
{
	nc_reply * reply;

	if ((reply = nc_msg_build (reply_dump)) == NULL) {
		return (NULL);
	}
	reply->type.reply = nc_reply_get_type(reply);

	return (reply);
}

nc_reply* ncxml_reply_build(xmlDocPtr reply_dump)
{
	nc_reply * reply;

	if ((reply = ncxml_msg_build (reply_dump)) == NULL) {
		return NULL;
	}
	reply->type.reply = nc_reply_get_type(reply);

	return (reply);
}

const nc_msgid nc_reply_get_msgid(const nc_reply *reply)
{
	if (reply != NULL && reply != ((void *) -1)) {
		return (reply->msgid);
	} else {
		return (0);
	}
}

const nc_msgid nc_rpc_get_msgid(const nc_rpc *rpc)
{
	if (rpc != NULL) {
		return (rpc->msgid);
	} else {
		return (0);
	}
}

NC_OP nc_rpc_get_op(const nc_rpc *rpc)
{
	xmlNodePtr root, auxnode;

	if (rpc == NULL || rpc->doc == NULL) {
		ERROR("%s: Invalid parameter (missing message or message document).", __func__);
		return (NC_OP_UNKNOWN);
	}

	if ((root = xmlDocGetRootElement (rpc->doc)) == NULL || root->children == NULL) {
		ERROR("%s: Invalid parameter (invalid message structure).", __func__);
		return (NC_OP_UNKNOWN);
	}

	if (xmlStrcmp(root->name, BAD_CAST "rpc") != 0) {
		ERROR("%s: Invalid rpc message - not an <rpc> message.", __func__);
		return (NC_OP_UNKNOWN);
	}

	auxnode = root->children;
	while(1) {
		if (auxnode == NULL) {
			/* valid rpc operation not found */
			return (NC_OP_UNKNOWN);
			break;
		}
		if (auxnode->type != XML_ELEMENT_NODE) {
			/* not interesting node, go to another */
			auxnode = auxnode->next;
			continue;
		}
		/* check known rpc operations */
		if ((xmlStrcmp(auxnode->name, BAD_CAST "copy-config") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_COPYCONFIG);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "delete-config") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_DELETECONFIG);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "edit-config") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_EDITCONFIG);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "get") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_GET);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "get-config") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_GETCONFIG);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "get-schema") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_MONITORING) == 0)) {
			return (NC_OP_GETSCHEMA);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "lock") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_LOCK);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "unlock") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_UNLOCK);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "commit") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_COMMIT);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "discard-changes") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_DISCARDCHANGES);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "kill-session") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_KILLSESSION);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "close-session") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_BASE10) == 0)) {
			return (NC_OP_CLOSESESSION);
		} else if ((xmlStrcmp(auxnode->name, BAD_CAST "create-subscription") == 0) &&
				(xmlStrcmp(auxnode->ns->href, BAD_CAST NC_NS_NOTIFICATIONS) == 0)) {
			return (NC_OP_CREATESUBSCRIPTION);
		} else {
			/* try another one */
			auxnode = auxnode->next;
			continue;
		}
	}
	return (NC_OP_UNKNOWN);
}

char* nc_rpc_get_op_content (const nc_rpc* rpc)
{
	char *retval = NULL;
	xmlDocPtr aux_doc;
	xmlNodePtr root;
	xmlBufferPtr buffer;
	xmlXPathObjectPtr result = NULL;
	int i;

	if (rpc == NULL || rpc->doc == NULL) {
		return NULL;
	}

	result = xmlXPathEvalExpression(BAD_CAST  "/"NC_NS_BASE10_ID":rpc/*", rpc->ctxt);
	if (result != NULL && !xmlXPathNodeSetIsEmpty(result->nodesetval) ) {
		buffer = xmlBufferCreate();
		if (buffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			return NULL;
		}
		if ((root = xmlDocGetRootElement (rpc->doc)) == NULL) {
			return NULL;
		}

		/* by copying node, move all needed namespaces into the content nodes */
		aux_doc = xmlNewDoc(BAD_CAST "1.0");
		xmlDocSetRootElement(aux_doc, xmlCopyNodeList(root->children));
		for (i = 0; i < result->nodesetval->nodeNr; i++) {
			xmlNodeDump (buffer, aux_doc, result->nodesetval->nodeTab[i], 1, 1);
		}
		retval = strdup((char *)xmlBufferContent (buffer));
		xmlXPathFreeObject(result);
		xmlBufferFree (buffer);
		xmlFreeDoc(aux_doc);
	}

	return retval;
}

xmlNodePtr ncxml_rpc_get_op_content(const nc_rpc *rpc)
{
	xmlNodePtr root;

	if (rpc == NULL || rpc->doc == NULL) {
		return NULL;
	}

	if ((root = xmlDocGetRootElement (rpc->doc)) == NULL) {
		return NULL;
	}

	return (xmlCopyNodeList(root->children));
}

NC_RPC_TYPE nc_rpc_get_type(nc_rpc *rpc)
{
	NC_OP op;

	if (rpc != NULL) {
		if (rpc->type.rpc == NC_RPC_UNKNOWN && rpc->doc != NULL) {
			/* try to detect the type from the message body (according to the operation) */
			op = nc_rpc_get_op (rpc);
			switch (op) {
			case (NC_OP_GETCONFIG):
			case (NC_OP_GETSCHEMA):
			case (NC_OP_GET):
				rpc->type.rpc = NC_RPC_DATASTORE_READ;
				break;
			case (NC_OP_EDITCONFIG):
			case (NC_OP_COPYCONFIG):
			case (NC_OP_DELETECONFIG):
			case (NC_OP_LOCK):
			case (NC_OP_UNLOCK):
			case (NC_OP_COMMIT):
			case (NC_OP_DISCARDCHANGES):
				rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
				break;
			case (NC_OP_CLOSESESSION):
			case (NC_OP_KILLSESSION):
			case (NC_OP_CREATESUBSCRIPTION):
				rpc->type.rpc = NC_RPC_SESSION;
				break;
			default:
				rpc->type.rpc = NC_RPC_UNKNOWN;
				break;
			}
		}
		return (rpc->type.rpc);
	} else {
		return (NC_RPC_UNKNOWN);
	}
}

/**
 * @brief Get source or target datastore type
 * @param rpc RPC message
 * @param ds_type 'target' or 'source'
 */
static NC_DATASTORE nc_rpc_get_ds (const nc_rpc *rpc, const char* ds_type)
{
	xmlXPathObjectPtr query_result = NULL;
	NC_DATASTORE retval = NC_DATASTORE_ERROR;
	int i;
	char** queries = NULL;
	static char* srcs[] = {
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":candidate",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":running",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":startup",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":url",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":source/"NC_NS_BASE10_ID":config"
	};
	static char* trgs[] = {
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":candidate",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":running",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":startup",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":url",
			"/"NC_NS_BASE10_ID":rpc/*/"NC_NS_BASE10_ID":target/"NC_NS_BASE10_ID":config"
	};
	static NC_DATASTORE retvals[] = {
			NC_DATASTORE_CANDIDATE,
			NC_DATASTORE_RUNNING,
			NC_DATASTORE_STARTUP,
			NC_DATASTORE_URL,
			NC_DATASTORE_CONFIG
	};
#define nc_rpc_get_ds_RETVALS_COUNT 5

	if (rpc == NULL || rpc->doc == NULL || rpc->ctxt == NULL) {
		ERROR("%s: invalid rpc parameter", __func__);
		return NC_DATASTORE_ERROR;
	}

	if (strcmp(ds_type, "source") == 0) {
		queries = srcs;
	} else if (strcmp(ds_type, "target") == 0) {
		queries = trgs;
	} else {
		ERROR("%s: invalid ds_type parameter (%s)", __func__, ds_type);
		return NC_DATASTORE_ERROR;
	}

	for (i = 0; i < nc_rpc_get_ds_RETVALS_COUNT; i++) {
		if ((query_result = xmlXPathEvalExpression(BAD_CAST queries[i], rpc->ctxt)) != NULL) {
			if (!xmlXPathNodeSetIsEmpty(query_result->nodesetval) && query_result->nodesetval->nodeNr == 1) {
				retval = retvals[i];
				xmlXPathFreeObject(query_result);
				break;
			}
			xmlXPathFreeObject(query_result);
		}
	}

	return(retval);
}

NC_DATASTORE nc_rpc_get_source (const nc_rpc *rpc)
{
	return (nc_rpc_get_ds(rpc, "source"));
}

NC_DATASTORE nc_rpc_get_target (const nc_rpc *rpc)
{
	return (nc_rpc_get_ds(rpc, "target"));
}

static char* nc_rpc_get_copyconfig(const nc_rpc* rpc)
{
	xmlNodePtr rpc_root, op, source, config, aux_node;
	xmlDocPtr aux_doc;
	xmlBufferPtr resultbuffer;
	char * retval = NULL;

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NULL;
	}

	if ((op = rpc_root->children) == NULL) {
		return NULL;
	}

	source = op->children;
	while (source != NULL && !xmlStrEqual(source->name, BAD_CAST "source")) {
		source = source->next;
	}

	if (source == NULL) {
		return NULL;
	}

	config = source->children;
	while (config != NULL && !xmlStrEqual(config->name, BAD_CAST "config")) {
		config = config->next;
	}

	if (config != NULL) {
		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			return NULL;
		}
		if (config->children == NULL) {
			/* config is empty */
			return (strdup(""));
		}
		/* by copying nodelist, move all needed namespaces into the editing nodes */
		aux_doc = xmlNewDoc(BAD_CAST "1.0");
		xmlDocSetRootElement(aux_doc, xmlNewNode(NULL, BAD_CAST "config"));
		xmlAddChildList(aux_doc->children, xmlDocCopyNodeList(aux_doc, config->children));
		for (aux_node = aux_doc->children->children; aux_node != NULL; aux_node = aux_node->next) {
			xmlNodeDump(resultbuffer, aux_doc, aux_node, 2, 1);
		}
		retval = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(aux_doc);
	}

	return retval;
}

static xmlNodePtr ncxml_rpc_get_copyconfig(const nc_rpc* rpc)
{
	xmlNodePtr rpc_root, op, source, config;

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NULL;
	}

	/* \todo Use xPath */

	if ((op = rpc_root->children) == NULL) {
		return NULL;
	}

	source = op->children;
	while (source != NULL && !xmlStrEqual(source->name, BAD_CAST "source")) {
		source = source->next;
	}

	if (source == NULL) {
		return NULL;
	}

	config = source->children;
	while (config != NULL && !xmlStrEqual(config->name, BAD_CAST "config")) {
		config = config->next;
	}

	return (xmlCopyNode(config, 1));
}

static char* nc_rpc_get_editconfig(const nc_rpc* rpc)
{
	xmlNodePtr rpc_root, op, config, aux_node;
	xmlDocPtr aux_doc;
	xmlBufferPtr resultbuffer;
	char * retval = NULL;

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NULL;
	}

	if ((op = rpc_root->children) == NULL) {
		return NULL;
	}

	config = op->children;
	while (config != NULL && !xmlStrEqual(config->name, BAD_CAST "config")) {
		config = config->next;
	}

	if (config != NULL) {
		/* dump the result */
		resultbuffer = xmlBufferCreate();
		if (resultbuffer == NULL) {
			ERROR("%s: xmlBufferCreate failed (%s:%d).", __func__, __FILE__, __LINE__);
			return NULL;
		}
		if (config->children == NULL) {
			/* config is empty */
			return (strdup(""));
		}
		/* by copying nodelist, move all needed namespaces into the editing nodes */
		aux_doc = xmlNewDoc(BAD_CAST "1.0");
		xmlDocSetRootElement(aux_doc, xmlNewNode(NULL, BAD_CAST "config"));
		xmlAddChildList(aux_doc->children, xmlDocCopyNodeList(aux_doc, config->children));
		for (aux_node = aux_doc->children->children; aux_node != NULL; aux_node = aux_node->next) {
			xmlNodeDump(resultbuffer, aux_doc, aux_node, 2, 1);
		}
		retval = strdup((char *) xmlBufferContent(resultbuffer));
		xmlBufferFree(resultbuffer);
		xmlFreeDoc(aux_doc);
	}

	return retval;
}

static xmlNodePtr ncxml_rpc_get_editconfig(const nc_rpc* rpc)
{
	xmlNodePtr rpc_root, op, config;

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NULL;
	}

	/* \todo Use xPath */

	if ((op = rpc_root->children) == NULL) {
		return NULL;
	}

	config = op->children;
	while (config != NULL && !xmlStrEqual(config->name, BAD_CAST "config")) {
		config = config->next;
	}

	return (xmlCopyNode(config, 1));
}

char* nc_rpc_get_config(const nc_rpc *rpc)
{
	switch(nc_rpc_get_op(rpc)) {
	case NC_OP_COPYCONFIG:
		return (nc_rpc_get_copyconfig(rpc));
		break;
	case NC_OP_EDITCONFIG:
		return (nc_rpc_get_editconfig(rpc));
		break;
	default:
		/* other operations do not have config parameter */
		return (NULL);
	}
}

xmlNodePtr ncxml_rpc_get_config(const nc_rpc *rpc)
{
	switch(nc_rpc_get_op(rpc)) {
	case NC_OP_COPYCONFIG:
		return (ncxml_rpc_get_copyconfig(rpc));
		break;
	case NC_OP_EDITCONFIG:
		return (ncxml_rpc_get_editconfig(rpc));
		break;
	default:
		/* other operations do not have config parameter */
		return (NULL);
	}
}

NC_EDIT_DEFOP_TYPE nc_rpc_get_defop (const nc_rpc *rpc)
{
	xmlNodePtr rpc_root, op, defop;
	NC_EDIT_DEFOP_TYPE retval = NC_EDIT_DEFOP_MERGE;

	/* only applicable on edit-config */
	if (nc_rpc_get_op(rpc) != NC_OP_EDITCONFIG) {
		return NC_EDIT_DEFOP_ERROR;
	}

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NC_EDIT_DEFOP_ERROR;
	}

	if ((op = rpc_root->children) == NULL) {
		return NC_EDIT_DEFOP_ERROR;
	}

	defop = op->children;
	while (defop != NULL && !xmlStrEqual(defop->name, BAD_CAST "default-operation")) {
		defop = defop->next;
	}

	if (defop != NULL) {
		if (xmlStrEqual(defop->children->content, BAD_CAST "merge")) {
			retval = NC_EDIT_DEFOP_MERGE;
		} else if (xmlStrEqual(defop->children->content, BAD_CAST "replace")) {
			retval = NC_EDIT_DEFOP_REPLACE;
		} else if (xmlStrEqual(defop->children->content, BAD_CAST "none")) {
			retval = NC_EDIT_DEFOP_NONE;
		}
	}

	return retval;
}

NC_EDIT_ERROPT_TYPE nc_rpc_get_erropt (const nc_rpc *rpc)
{
	xmlNodePtr rpc_root, op, erropt;
	NC_EDIT_ERROPT_TYPE retval = NC_EDIT_ERROPT_STOP;

	/* only applicable on edit-config */
	if (nc_rpc_get_op(rpc) != NC_OP_EDITCONFIG) {
		return NC_EDIT_ERROPT_ERROR;
	}

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NC_EDIT_ERROPT_ERROR;
	}

	if ((op = rpc_root->children) == NULL) {
		return NC_EDIT_ERROPT_ERROR;
	}

	erropt = op->children;
	while (erropt != NULL && !xmlStrEqual(erropt->name, BAD_CAST "error-option")) {
		erropt = erropt->next;
	}

	if (erropt != NULL) {
		if (xmlStrEqual(erropt->children->content, BAD_CAST "stop-on-error")) {
			retval = NC_EDIT_ERROPT_STOP;
		} else if (xmlStrEqual(erropt->children->content, BAD_CAST "continue-on-error")) {
			retval = NC_EDIT_ERROPT_CONT;
		} else if (xmlStrEqual(erropt->children->content, BAD_CAST "rollback-on-error")) {
			retval = NC_EDIT_ERROPT_ROLLBACK;
		}
	}

	return retval;
}

NC_EDIT_TESTOPT_TYPE nc_rpc_get_testopt (const nc_rpc *rpc)
{
	xmlXPathObjectPtr query_result = NULL;
	xmlNodePtr node;
	NC_EDIT_TESTOPT_TYPE retval = NC_EDIT_TESTOPT_NOTSET;

	/* only applicable on edit-config */
	if (nc_rpc_get_op(rpc) != NC_OP_EDITCONFIG) {
		return NC_EDIT_TESTOPT_ERROR;
	}

	if ((query_result = xmlXPathEvalExpression(BAD_CAST "/"NC_NS_BASE10_ID":rpc/"NC_NS_BASE10_ID":edit-config/"NC_NS_BASE10_ID":test-option", rpc->ctxt)) != NULL) {
		if (xmlXPathNodeSetIsEmpty(query_result->nodesetval)) {
			retval = NC_EDIT_TESTOPT_NOTSET;
		} else if (query_result->nodesetval->nodeNr > 1) {
			ERROR("%s: invalid rpc message (multiple test-option elements)", __func__);
			retval = NC_EDIT_TESTOPT_ERROR;
		} else {
			for (node = query_result->nodesetval->nodeTab[0]->children; node != NULL && node->type != XML_TEXT_NODE; node = node->next);

			if (node != NULL) {
				if (xmlStrcmp(node->content, BAD_CAST "set") == 0) {
					retval = NC_EDIT_TESTOPT_SET;
				} else if (xmlStrcmp(node->content, BAD_CAST "test-only") == 0) {
					retval = NC_EDIT_TESTOPT_TEST;
				} else if (xmlStrcmp(node->content, BAD_CAST "test-then-set") == 0) {
					retval = NC_EDIT_TESTOPT_TESTSET;
				} else {
					ERROR("%s: invalid value of test-option element (%s)", __func__, node->content);
					retval = NC_EDIT_TESTOPT_ERROR;
				}
			} else {
				ERROR("%s: invalid content of test-option element", __func__);
				retval = NC_EDIT_TESTOPT_ERROR;
			}
		}
		xmlXPathFreeObject(query_result);
	}

	return (retval);
}

struct nc_filter * nc_rpc_get_filter (const nc_rpc * rpc)
{
	struct nc_filter * retval = NULL;
	xmlNodePtr filter_node;
	xmlChar *type_string;
	NC_OP op;

	if (rpc != NULL && rpc->doc != NULL &&
			rpc->doc->children != NULL &&
			rpc->doc->children->children != NULL &&
			rpc->doc->children->children->children != NULL) {
		op = nc_rpc_get_op(rpc);
		if (op == NC_OP_GET || op == NC_OP_GETCONFIG || op == NC_OP_CREATESUBSCRIPTION) {
			/* doc -> <rpc> -> <op> -> <param> */
			filter_node = rpc->doc->children->children->children;
			while (filter_node) {
				/* \todo Check the namespace */
				if (xmlStrEqual(filter_node->name, BAD_CAST "filter")) {
					retval = malloc(sizeof(struct nc_filter));
					type_string = xmlGetProp(filter_node, BAD_CAST "type");
					/* set filter type */
					if (type_string == NULL) {
						/* implicit filter type is NC_FILTER_SUBTREE */
						retval->type = NC_FILTER_SUBTREE;
					} else if (xmlStrcmp(type_string, BAD_CAST "subtree") == 0) {
						retval->type = NC_FILTER_SUBTREE;
						retval->subtree_filter = xmlCopyNode(filter_node, 1);
					} else {
						/* some uknown filter type */
						retval->type = NC_FILTER_UNKNOWN;
					}

					/* process only the first <filter> node, ignore the rest */
					break;
				}
				filter_node = filter_node->next;
			}
		}
	}
	return retval;
}

NC_REPLY_TYPE nc_reply_get_type(nc_reply *reply)
{
	xmlNodePtr root, auxnode;

	if (reply != NULL && reply != ((void *) -1)) {
		if (reply->type.reply == NC_REPLY_UNKNOWN && reply->doc != NULL) {
			/* try to detect the type from the message body */
			if ((root = xmlDocGetRootElement (reply->doc)) == NULL) {
				return (NC_REPLY_UNKNOWN);
			}
			auxnode = root->children;
			while(1) {
				if (auxnode == NULL) {
					/* valid rpc-reply type not found */
					reply->type.reply = NC_REPLY_UNKNOWN;
					break;
				}
				if (auxnode->type != XML_ELEMENT_NODE) {
					/* not interesting node, go to another */
					auxnode = auxnode->next;
					continue;
				}
				/* check known rpc-reply types */
				if (xmlStrcmp (auxnode->name, BAD_CAST "ok") == 0) {
					reply->type.reply = NC_REPLY_OK;
					break;
				} else if (xmlStrcmp (auxnode->name, BAD_CAST "rpc-error") == 0) {
					reply->type.reply = NC_REPLY_ERROR;
					reply->error = nc_msg_parse_error(reply);
					break;
				} else if (xmlStrcmp (auxnode->name, BAD_CAST "data") == 0) {
					reply->type.reply = NC_REPLY_DATA;
					break;
				} else {
					/* try another one */
					auxnode = auxnode->next;
					continue;
				}
			}
		}

		return (reply->type.reply);
	} else {
		return (NC_REPLY_UNKNOWN);
	}
}

char *nc_reply_get_data(const nc_reply *reply)
{
	char *buf;
	xmlBufferPtr data_buf;
	xmlNodePtr inside_data, aux_data;
	xmlDocPtr aux_doc;

	if (reply == NULL || reply == ((void *) -1) ||
			reply->type.reply != NC_REPLY_DATA ||
			reply->doc == NULL ||
			reply->doc->children == NULL || /* <rpc-reply> */
			reply->doc->children->children == NULL /* <data> */) {
		/* some part of the reply is corrupted */
		ERROR("nc_reply_get_data: invalid input parameter.");
		return (NULL);
	}
	inside_data = reply->doc->children->children->children;
	if (inside_data == NULL) { /* content */
		/*
		 * Returned data content is empty, so return empty
		 * string without any error message. This can be a valid
		 * content of the reply, e.g. in case of filtering.
		 */
		return (strdup(""));
	}

	if ((data_buf = xmlBufferCreate()) == NULL) {
		return NULL;
	}

	aux_doc = xmlNewDoc(BAD_CAST "1.0");
	xmlDocSetRootElement(aux_doc, xmlNewNode(NULL, BAD_CAST "data"));
	xmlAddChildList(aux_doc->children, xmlDocCopyNodeList(aux_doc, inside_data));
	for (aux_data = aux_doc->children->children; aux_data != NULL; aux_data = aux_data->next) {
		xmlNodeDump(data_buf, aux_doc, aux_data, 1, 1);
	}
	buf = strdup((char*) xmlBufferContent(data_buf));
	xmlBufferFree(data_buf);
	xmlFreeDoc(aux_doc);

	return (buf);
}

xmlNodePtr ncxml_reply_get_data(const nc_reply *reply)
{
	/* \todo use xPath */

	if (reply == NULL || reply == ((void *) -1) ||
			reply->type.reply != NC_REPLY_DATA ||
			reply->doc == NULL ||
			reply->doc->children == NULL || /* <rpc-reply> */
			reply->doc->children->children == NULL /* <data> */) {
		/* some part of the reply is corrupted */
		ERROR("nc_reply_get_data: invalid input parameter.");
		return (NULL);
	}

	return (xmlCopyNode(reply->doc->children->children, 1));
}

const char *nc_reply_get_errormsg(nc_reply *reply)
{
	if (reply == NULL || reply == ((void *) -1) || reply->type.reply != NC_REPLY_ERROR) {
		return (NULL);
	}

	if (reply->error == NULL) {
		/* parse all error information */
		nc_err_parse(reply);
	}

	return ((reply->error == NULL) ? NULL : reply->error->message);
}

nc_rpc *nc_msg_client_hello(char **cpblts)
{
	nc_rpc *msg;
	xmlNodePtr node;
	int i;
	xmlNsPtr ns;

	if (cpblts == NULL || cpblts[0] == NULL) {
		ERROR("hello: no capability specified");
		return (NULL);
	}

	msg = calloc(1, sizeof(nc_rpc));
	if (msg == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	msg->error = NULL;
	msg->doc = xmlNewDoc(BAD_CAST "1.0");
	msg->doc->encoding = xmlStrdup(BAD_CAST UTF8);
	msg->msgid = NULL;
	msg->with_defaults = NCWD_MODE_NOTSET;
	msg->type.rpc = NC_RPC_HELLO;

	/* create root element */
	msg->doc->children = xmlNewDocNode(msg->doc, NULL, BAD_CAST NC_HELLO_MSG, NULL);

	/* set namespace */
	ns = xmlNewNs(msg->doc->children, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(msg->doc->children, ns);

	/* create capabilities node */
	node = xmlNewChild(msg->doc->children, NULL, BAD_CAST "capabilities", NULL);
	for (i = 0; cpblts[i] != NULL; i++) {
		xmlNewChild(node, NULL, BAD_CAST "capability", BAD_CAST cpblts[i]);
	}

	/* create xpath evaluation context */
	if ((msg->ctxt = xmlXPathNewContext(msg->doc)) == NULL) {
		ERROR("%s: rpc message XPath context can not be created.", __func__);
		nc_msg_free(msg);
		return NULL;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(msg->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(msg);
		return NULL;
	}

	return (msg);
}

void nc_msg_free(struct nc_msg *msg)
{
	struct nc_err* e, *efree;

	if (msg != NULL && msg != ((void *) -1)) {
		if (msg->doc != NULL) {
			xmlFreeDoc(msg->doc);
		}
		if (msg->ctxt != NULL) {
			xmlXPathFreeContext(msg->ctxt);
		}
		if ((e = msg->error) != NULL) {
			while(e != NULL) {
				efree = e;
				e = e->next;
				nc_err_free(efree);
			}
		}
		if (msg->msgid != NULL) {
			free(msg->msgid);
		}
		free(msg);
	}
}

void nc_rpc_free(nc_rpc *rpc)
{
	nc_msg_free((struct nc_msg*) rpc);
}

void nc_reply_free(nc_reply *reply)
{
	nc_msg_free((struct nc_msg*) reply);
}

struct nc_msg *nc_msg_dup(struct nc_msg *msg)
{
	struct nc_msg *dupmsg;

	if (msg == NULL || msg == ((void *) -1) || msg->doc == NULL) {
		return (NULL);
	}

	dupmsg = calloc(1, sizeof(struct nc_msg));
	dupmsg->doc = xmlCopyDoc(msg->doc, 1);
	dupmsg->type = msg->type;
	dupmsg->with_defaults = msg->with_defaults;
	if (msg->msgid != NULL) {
		dupmsg->msgid = strdup(msg->msgid);
	} else {
		dupmsg->msgid = NULL;
	}
	if (msg->error != NULL) {
		dupmsg->error = nc_err_dup(msg->error);
	} else {
		dupmsg->error = NULL;
	}

	/* create xpath evaluation context */
	if ((dupmsg->ctxt = xmlXPathNewContext(dupmsg->doc)) == NULL) {
		ERROR("%s: rpc message XPath context can not be created.", __func__);
		nc_msg_free(dupmsg);
		return NULL;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(dupmsg->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(dupmsg);
		return NULL;
	}

	return (dupmsg);
}

nc_rpc *nc_rpc_dup(const nc_rpc* rpc)
{
	return ((nc_rpc*)nc_msg_dup((struct nc_msg*)rpc));
}

nc_reply *nc_reply_dup(const nc_reply* reply)
{
	return ((nc_reply*)nc_msg_dup((struct nc_msg*)reply));
}

nc_rpc *nc_msg_server_hello(char **cpblts, char* session_id)
{
	nc_rpc *msg;

	msg = nc_msg_client_hello(cpblts);
	if (msg == NULL) {
		return (NULL);
	}
	msg->error = NULL;

	/* assign session-id */
	/* check if session-id is prepared */
	if (session_id == NULL || strlen(session_id) == 0) {
		/* no session-id set */
		ERROR("Hello: session ID is empty");
		xmlFreeDoc(msg->doc);
		free(msg);
		return (NULL);
	}

	/* create <session-id> node */
	xmlNewChild(msg->doc->children, NULL, BAD_CAST "session-id", BAD_CAST session_id);

	return (msg);
}

/**
 * @brief Create generic NETCONF message envelope according to given type (rpc or rpc-reply) and insert given data
 *
 * @param[in] content pointer to xml node containing data
 * @param[in] msgtype string of the envelope element (rpc, rpc-reply)
 *
 * @return Prepared nc_msg structure.
 */
struct nc_msg* nc_msg_create(const xmlNodePtr content, char* msgtype)
{
	struct nc_msg* msg;

	xmlDocPtr xmlmsg = NULL;
	xmlNsPtr ns;

	if (content == NULL) {
		ERROR("%s: Invalid \'content\' parameter.", __func__);
		return (NULL);
	}

	if ((xmlmsg = xmlNewDoc(BAD_CAST XML_VERSION)) == NULL) {
		ERROR("xmlNewDoc failed (%s:%d).", __FILE__, __LINE__);
		return NULL;
	}
	xmlmsg->encoding = xmlStrdup(BAD_CAST UTF8);

	if ((xmlmsg->children = xmlNewDocNode(xmlmsg, NULL, BAD_CAST msgtype, NULL)) == NULL) {
		ERROR("xmlNewDocNode failed (%s:%d).", __FILE__, __LINE__);
		xmlFreeDoc(xmlmsg);
		return NULL;
	}

	/* set namespace */
	ns = xmlNewNs(xmlmsg->children, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(xmlmsg->children, ns);

	if (xmlAddChild(xmlmsg->children, xmlCopyNode(content, 1)) == NULL) {
		ERROR("xmlAddChild failed (%s:%d).", __FILE__, __LINE__);
		xmlFreeDoc(xmlmsg);
		return NULL;
	}

	msg = calloc(1, sizeof(nc_rpc));
	if (msg == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	msg->doc = xmlmsg;
	msg->msgid = NULL;
	msg->error = NULL;
	msg->with_defaults = NCWD_MODE_NOTSET;

	/* create xpath evaluation context */
	if ((msg->ctxt = xmlXPathNewContext(msg->doc)) == NULL) {
		ERROR("%s: rpc message XPath context can not be created.", __func__);
		nc_msg_free(msg);
		return NULL;
	}

	/* register base namespace for the rpc */
	if (xmlXPathRegisterNs(msg->ctxt, BAD_CAST NC_NS_BASE10_ID, BAD_CAST NC_NS_BASE10) != 0) {
		ERROR("Registering base namespace for the message xpath context failed.");
		nc_msg_free(msg);
		return NULL;
	}

	/* remove duplicated namespace definitions */
	xmlDOMWrapReconcileNamespaces(NULL, msg->doc->children, 1);

	return (msg);
}

/**
 * @brief Create \<rpc\> envelope and insert given data
 *
 * @param[in] content pointer to xml node containing data
 *
 * @return Prepared nc_rpc structure.
 */
static nc_rpc* nc_rpc_create(const xmlNodePtr content)
{
	return ((nc_rpc*)nc_msg_create(content, "rpc"));
}

/**
 * @brief Create \<rpc-reply\> envelope and insert given data
 *
 * @param[in] content pointer to xml node containing data
 *
 * @return Prepared nc_reply structure.
 */
static nc_reply* nc_reply_create(const xmlNodePtr content)
{
	return ((nc_reply*)nc_msg_create(content,"rpc-reply"));
}

nc_reply *nc_reply_ok()
{
	nc_reply *reply;
	xmlNodePtr content;
	xmlNsPtr ns;

	if ((content = xmlNewNode(NULL, BAD_CAST "ok")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	reply = nc_reply_create(content);
	reply->type.reply = NC_REPLY_OK;
	xmlFreeNode(content);

	return (reply);
}

nc_reply *nc_reply_data(const char* data)
{
	nc_reply *reply;
	xmlDocPtr doc_data;
	char* data_env;
	struct nc_err* e;

	if (data != NULL) {
		if (asprintf(&data_env, "<data xmlns=\"%s\">%s</data>", NC_NS_BASE10, data) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
		}
	} else {
		if (asprintf(&data_env, "<data xmlns=\"%s\"/>", NC_NS_BASE10) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
		}
	}

	/* prepare XML structure from given data */
	doc_data = xmlReadMemory(data_env, strlen(data_env), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (doc_data == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		free(data_env);
		e = nc_err_new(NC_ERR_OP_FAILED);
		nc_err_set(e, NC_ERR_PARAM_MSG, "Configuration data seems to be corrupted.");
		return (nc_reply_error(e));
	}

	reply = nc_reply_create(doc_data->children);
	reply->type.reply = NC_REPLY_DATA;
	xmlFreeDoc(doc_data);
	free(data_env);

	return (reply);
}

nc_reply *ncxml_reply_data(const xmlNodePtr data)
{
	nc_reply *reply;
	xmlNodePtr content;
	xmlNsPtr ns;

	content = xmlNewNode(NULL, BAD_CAST "data");
	if (content == NULL) {
		ERROR("xmlNewNode failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (xmlAddChildList(content, xmlCopyNodeList(data)) == NULL) {
		ERROR("xmlAddChildList failed (%s:%d).", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	reply = nc_reply_create(content);
	reply->type.reply = NC_REPLY_DATA;
	xmlFreeNode(content);

	return (reply);
}

static xmlNodePtr new_reply_error_content(struct nc_err* error)
{
	xmlNodePtr content, einfo = NULL, first = NULL;
	xmlNsPtr ns = NULL;

	while (error != NULL) {
		if ((content = xmlNewNode(NULL, BAD_CAST "rpc-error")) == NULL) {
			ERROR("xmlNewNode failed (%s:%d).", __FILE__, __LINE__);
			return (NULL);
		}
		if (ns == NULL) {
			/* set namespace */
			ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
		}
		xmlSetNs(content, ns);

		if (error->type != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-type", BAD_CAST error->type) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		if (error->tag != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-tag", BAD_CAST error->tag) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		if (error->severity != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-severity", BAD_CAST error->severity) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		if (error->apptag != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-app-tag", BAD_CAST error->apptag) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		if (error->path != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-path", BAD_CAST error->path) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		if (error->message != NULL) {
			if (xmlNewChild(content, ns, BAD_CAST "error-message", BAD_CAST error->message) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		}

		/* error-info items */
		if (error->sid != NULL || error->attribute != NULL || error->element != NULL || error->ns != NULL) {
			/* prepare error-info */

			if ((einfo = xmlNewChild(content, ns, BAD_CAST "error-info", NULL)) == NULL) {
				ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}

			if (error->sid != NULL) {
				if (xmlNewChild(einfo, ns, BAD_CAST "session-id", BAD_CAST error->sid) == NULL) {
					ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
					xmlFreeNode(content);
					return (NULL);
				}
			}

			if (error->attribute != NULL) {
				if (xmlNewChild(einfo, ns, BAD_CAST "bad-attribute", BAD_CAST error->attribute) == NULL) {
					ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
					xmlFreeNode(content);
					return (NULL);
				}
			}

			if (error->element != NULL) {
				if (xmlNewChild(einfo, ns, BAD_CAST "bad-element", BAD_CAST error->element) == NULL) {
					ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
					xmlFreeNode(content);
					return (NULL);
				}
			}

			if (error->ns != NULL) {
				if (xmlNewChild(einfo, ns, BAD_CAST "bad-namespace", BAD_CAST error->ns) == NULL) {
					ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
					xmlFreeNode(content);
					return (NULL);
				}
			}
		}

		if (first == NULL) {
			first = content;
		} else {
			content->next = first;
			first = content;
		}
		error = error->next;
	}

	return(first);
}

nc_reply *nc_reply_error(struct nc_err* error)
{
	nc_reply *reply;
	xmlNodePtr content;

	if (error == NULL) {
		ERROR("Empty error structure to create rpc-error reply.");
		return (NULL);
	}

	if ((content = new_reply_error_content(error)) == NULL) {
		return (NULL);
	}
	if ((reply = nc_reply_create(content)) == NULL) {
		return (NULL);
	}
	reply->error = error;
	reply->type.reply = NC_REPLY_ERROR;
	xmlFreeNode(content);

	return (reply);
}

int nc_reply_error_add(nc_reply *reply, struct nc_err* error)
{
	xmlNodePtr content;

	if (error == NULL || reply == NULL || reply == ((void *) -1) || reply->type.reply != NC_REPLY_ERROR) {
		return (EXIT_FAILURE);
	}
	if (reply->doc == NULL || reply->doc->children == NULL) {
		return (EXIT_FAILURE);
	}

	/* prepare new <rpc-error> part */
	if ((content = new_reply_error_content(error)) == NULL) {
		return (EXIT_FAILURE);
	}

	/* add new description into the reply */
	if (xmlAddChild(reply->doc->children, xmlCopyNode(content, 1)) == NULL) {
		ERROR("xmlAddChild failed (%s:%d).", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (EXIT_FAILURE);
	}

	/* find last error */
	while (error->next != NULL) {
		error = error->next;
	}
	/* add error structure into the reply's list */
	error->next = reply->error;
	reply->error = error;

	/* cleanup */
	xmlFreeNode(content);

	return (EXIT_SUCCESS);
}

nc_reply * nc_reply_merge (int count, nc_reply * msg1, nc_reply * msg2, ...)
{
	nc_reply *merged_reply = NULL;
	nc_reply ** to_merge = NULL;
	NC_REPLY_TYPE type;
	va_list ap;
	int i, len = 0;
	char * tmp, * data = NULL;

	/* minimal 2 massages */
	if (count < 2) {
		VERB("Number of messages must be at least 2 (was %d)", count);
		return NULL;
	}

	/* get type and check that first two have the same type */
	if ((type = nc_reply_get_type(msg1)) != nc_reply_get_type(msg2) && type == NC_REPLY_UNKNOWN) {
		VERB("All messages to merge must be of the same type.");
		return NULL;
	}

	/* initialize argument vector */
	va_start (ap, msg2);

	/* allocate array for message pointers */
	to_merge = malloc(sizeof(nc_reply*) * count);
	to_merge[0] = msg1;
	to_merge[1] = msg2;

	/* get all messages and check their type */
	for (i=2; i<count; i++) {
		to_merge[i] = va_arg(ap,nc_reply*);
		if (type != nc_reply_get_type(to_merge[i])) {
			VERB("All messages to merge must be of the same type.");
			free (to_merge);
			va_end (ap);
			return NULL;
		}
	}

	/* finalize argument vector */
	va_end (ap);

	switch (type) {
	case NC_REPLY_OK:
		/* just OK */
		merged_reply = msg1;
		break;
	case NC_REPLY_DATA:
		/* join <data/> */
		for (i=0; i<count; i++) {
			tmp = nc_reply_get_data (to_merge[i]);
			if (data == NULL) {
				len = strlen (tmp);
				data = strdup (tmp);
			} else {
				len += strlen (tmp);
				data = realloc (data, sizeof(char)*(len+1));
				strcat (data, tmp);
			}
			free (tmp);
		}
		nc_reply_free(msg1);
		merged_reply = nc_reply_data(data);
		free(data);
		break;
	case NC_REPLY_ERROR:
		/* join all errors */
		merged_reply = msg1;
		for (i=1; i<count; i++) {
			nc_reply_error_add(merged_reply, to_merge[i]->error);
			to_merge[i]->error = NULL;
		}
		break;
	default:
		break;
	}

	/* clean all merged messages */
	for (i=1; i<count; i++) {
		nc_reply_free (to_merge[i]);
	}
	free (to_merge);

	return merged_reply;
}

nc_rpc *nc_rpc_closesession()
{
	nc_rpc *rpc;
	xmlNodePtr content;
	xmlNsPtr ns;

	if ((content = xmlNewNode(NULL, BAD_CAST "close-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_SESSION;
	}
	xmlFreeNode(content);

	return (rpc);
}

static int process_filter_param (xmlNodePtr content, const struct nc_filter* filter)
{
	xmlDocPtr doc_filter = NULL;

	if (filter != NULL) {
		if (filter->type == NC_FILTER_SUBTREE && filter->subtree_filter != NULL) {
			/* process Subtree filter type */

			if (xmlAddChild(content, xmlCopyNode(filter->subtree_filter, 1)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeDoc(doc_filter);
				return (EXIT_FAILURE);
			}
		} else {
			WARN("%s: unknown filter type used - skipping filter.", __func__);
		}
	}
	return (EXIT_SUCCESS);
}

int nc_rpc_capability_attr(nc_rpc* rpc, NC_CAP_ATTR attr, ...)
{
	va_list argp;
	xmlNodePtr root, newnode;
	xmlNsPtr ns;
	NCWD_MODE mode;
	char *wd_mode;

	if (rpc == NULL) {
		ERROR("%s: invalid RPC to modify.", __func__);
		return (EXIT_FAILURE);
	}

	va_start(argp, attr);

	switch(attr) {
	case NC_CAP_ATTR_WITHDEFAULTS_MODE:
		/* check operation from rpc */
		switch(nc_rpc_get_op(rpc)) {
		case NC_OP_GET:
		case NC_OP_GETCONFIG:
		case NC_OP_COPYCONFIG:
			/* ok */
			break;
		default:
			ERROR("%s: required operation (id %d) is not applicable to the given RPC message.", __func__, attr);
			va_end(argp);
			return (EXIT_FAILURE);
		}

		/* get variadic argument */
		mode = va_arg(argp, NCWD_MODE);

		if (mode != NCWD_MODE_NOTSET) {
			switch (mode) {
			case NCWD_MODE_ALL:
				wd_mode = "report-all";
				break;
			case NCWD_MODE_ALL_TAGGED:
				wd_mode = "report-all-tagged";
				break;
			case NCWD_MODE_TRIM:
				wd_mode = "trim";
				break;
			case NCWD_MODE_EXPLICIT:
				wd_mode = "explicit";
				break;
			default:
				ERROR("%s: Invalid with-defaults mode specified.", __func__);
				va_end(argp);
				return(EXIT_FAILURE);
				break;
			}

			/* go ot the operation element*/
			root = xmlDocGetRootElement(rpc->doc);
			/* \todo access required node via xpath */

			newnode = xmlNewChild (root->children, NULL, BAD_CAST "with-defaults", BAD_CAST wd_mode);
			if (newnode == NULL) {
				ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
				va_end(argp);
				return (EXIT_FAILURE);
			}
			ns = xmlNewNs(newnode, BAD_CAST NC_NS_WITHDEFAULTS, NULL);
			xmlSetNs(newnode, ns);
		} else {
			/* \todo remove \<with-defaults\> element if exists */
		}
		rpc->with_defaults = mode;

		break;
	default:
		ERROR("%s: required operation (id %d) is not supported.", __func__, attr);
		va_end(argp);
		return (EXIT_FAILURE);
		break;
	}

	va_end(argp);
	return (EXIT_SUCCESS);
}

nc_rpc *nc_rpc_getconfig(NC_DATASTORE source, const struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlNodePtr content, node;
	xmlNsPtr ns;
	char* datastore;


	switch (source) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown source datastore for <get-config>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "get-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	/* source */
	node = xmlNewChild(content, ns, BAD_CAST "source", NULL);
	if (node == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if (xmlNewChild(node, ns, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* add filter specification if any required */
	if (process_filter_param(content, filter) != 0) {
		xmlFreeNode(content);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_READ;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_get(const struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlNodePtr content;
	xmlNsPtr ns;

	if ((content = xmlNewNode(NULL, BAD_CAST "get")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	/* add filter specification if any required */
	if (process_filter_param(content, filter) != 0) {
		xmlFreeNode(content);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_READ;
	}
	xmlFreeNode(content);

	return (rpc);

}

nc_rpc *nc_rpc_deleteconfig(NC_DATASTORE target, ...)
{
	nc_rpc *rpc;
	va_list argp;
	xmlNodePtr content, node_target;
	xmlNsPtr ns;
	char* datastore = NULL;
	const char* url;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		ERROR("Running datastore cannot be deleted.");
		return (NULL);
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	case NC_DATASTORE_URL:
		/* work is done later */
		break;
	default:
		ERROR("Unknown target datastore for <delete-config>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "delete-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	node_target = xmlNewChild(content, ns, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if (target == NC_DATASTORE_URL) {
		/* url */
		va_start(argp, target);
		url = va_arg(argp, const char*);
		if (xmlNewChild(node_target, ns, BAD_CAST "url", BAD_CAST url) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			va_end(argp);
			return (NULL);
		}
		va_end(argp);
	} else {
		/* running, startup, candidate */
		if (xmlNewChild(node_target, ns, BAD_CAST datastore, NULL) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_lock(NC_DATASTORE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	xmlNsPtr ns;
	char* datastore;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <lock>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "lock")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	node_target = xmlNewChild(content, ns, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, ns, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_unlock(NC_DATASTORE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	xmlNsPtr ns;
	char* datastore;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <unlock>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "unlock")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	node_target = xmlNewChild(content, ns, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, ns, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
	xmlFreeNode(content);

	return (rpc);
}

static nc_rpc *_rpc_copyconfig(NC_DATASTORE source, NC_DATASTORE target, const xmlNodePtr config, const char* source_url, const char* target_url)
{
	nc_rpc *rpc = NULL;
	xmlNodePtr content, node_target, node_source, node_config;
	xmlNsPtr ns;
	NC_DATASTORE params[2] = {source, target};
	char *datastores[2] = {NULL, NULL}; /* 0 - source, 1 - target */
	int i;

	if (target == source) {
		ERROR("<copy-config>'s source and target parameters identify the same datastore.");
		return (NULL);
	}

	for (i = 0; i < 2; i++) {
		switch (params[i]) {
		case NC_DATASTORE_RUNNING:
			datastores[i] = "running";
			break;
		case NC_DATASTORE_STARTUP:
			datastores[i] = "startup";
			break;
		case NC_DATASTORE_CANDIDATE:
			datastores[i] = "candidate";
			break;
		case NC_DATASTORE_CONFIG:
			if (i == 1) {
				ERROR("Unknown target datastore for <copy-config>.");
				return (NULL);
			}
			break;
		case NC_DATASTORE_URL:
			if (i == 0) {
				if (source_url == NULL) {
					ERROR("Missing URL specification for <copy-config>'s source.");
					return (NULL);
				}
			} else { /* i == 1 */
				if (target_url == NULL) {
					ERROR("Missing URL specification for <copy-config>'s target.");
					return (NULL);
				}
			}
			break;
		default:
			ERROR("Unknown %s datastore for <copy-config>.", (i == 0) ? "source" : "target");
			return (NULL);
			break;
		}
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "copy-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	/* <source> */
	node_source = xmlNewChild(content, ns, BAD_CAST "source", NULL);
	if (node_source == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		goto cleanup;
	}
	if (datastores[0] == NULL) {
		/* source configuration is given as data parameter */

		/* if data empty string, create \<copy-config\> with empty \<config\> */
		/* only if data is not empty string, fill \<config\> */
		/* RFC 6241 defines \<config\> as anyxml and thus it can be empty */

		/* create empty config element in rpc request */
		if ((node_config = xmlNewChild(node_source, ns, BAD_CAST "config", NULL)) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
		if (config != NULL) {
			/* connect given configuration data with the rpc request */
			if (xmlAddChildList(node_config, xmlCopyNodeList(config)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				goto cleanup;
			}
		}
	} else if (source_url != NULL) {
		/* source is specified as URL */
		if (xmlNewChild(node_source, ns, BAD_CAST "url", BAD_CAST source_url) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	} else {
		/* source is one of the standard datastores */
		if (xmlNewChild(node_source, ns, BAD_CAST datastores[0], NULL) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	}

	/* <target> */
	node_target = xmlNewChild(content, ns, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		goto cleanup;
	}
	if (target_url != NULL) {
		/* source is specified as URL */
		if (xmlNewChild(node_source, ns, BAD_CAST "url", BAD_CAST target_url) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	} else {
		/* standard datastore */
		if (xmlNewChild(node_target, ns, BAD_CAST datastores[1], NULL) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}

cleanup:
	xmlFreeNode(content);
	return (rpc);

}

nc_rpc *ncxml_rpc_copyconfig(NC_DATASTORE source, NC_DATASTORE target, ...)
{
	xmlNodePtr config = NULL;
	const char *source_url = NULL, *target_url = NULL;
	nc_rpc* retval;
	va_list argp;

	va_start(argp, target);

	if (source == NC_DATASTORE_CONFIG) {
		config = va_arg(argp, xmlNodePtr);
	} else if (source == NC_DATASTORE_URL) {
		source_url = va_arg(argp, const char*);
	}

	if (target == NC_DATASTORE_URL) {
		target_url = va_arg(argp, const char*);
	}

	retval = _rpc_copyconfig(source, target, config, source_url, target_url);
	va_end(argp);
	return (retval);
}

nc_rpc *nc_rpc_copyconfig(NC_DATASTORE source, NC_DATASTORE target, ...)
{
	xmlDocPtr config = NULL;
	const char* config_s;
	char* config_S;
	const char *source_url = NULL, *target_url = NULL;
	nc_rpc* retval;
	va_list argp;

	va_start(argp, target);

	if (source == NC_DATASTORE_CONFIG) {
		config_s = va_arg(argp, const char*);

		/* transform string to the xmlNodePtr */
		/* add covering <config> element to allow to specify multiple root elements */
		if (asprintf(&config_S, "<config>%s</config>", config_s) == -1) {
			ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
			va_end(argp);
			return (NULL);
		}

		/* prepare XML structure from given data */
		config = xmlReadMemory(config_S, strlen(config_S), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		free(config_S);
		if (config == NULL) {
			ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
			va_end(argp);
			return (NULL);
		}
	} else if (source == NC_DATASTORE_URL) {
		source_url = va_arg(argp, const char*);
	}

	if (target == NC_DATASTORE_URL) {
		target_url = va_arg(argp, const char*);
	}

	retval = _rpc_copyconfig(source, target, (config != NULL) ? config->children->children : NULL, source_url, target_url);
	va_end(argp);
	xmlFreeDoc(config);
	return (retval);
}

static nc_rpc *_rpc_editconfig(NC_DATASTORE target, NC_DATASTORE source, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, NC_EDIT_TESTOPT_TYPE test_option, const xmlNodePtr config, const char* source_url)
{
	nc_rpc *rpc = NULL;
	xmlNodePtr content, node_target, node_defop, node_config;
	xmlNsPtr ns;
	char* datastore, *defop = NULL, *erropt = NULL, *testopt = NULL;

	/* detect target datastore */
	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <edit-config>.");
		return (NULL);
		break;
	}


	/* detect default-operation parameter */
	if (default_operation != 0) {
		switch (default_operation) {
		case NC_EDIT_DEFOP_MERGE:
			defop = "merge";
			break;
		case NC_EDIT_DEFOP_NONE:
			defop = "none";
			break;
		case NC_EDIT_DEFOP_REPLACE:
			defop = "replace";
			break;
		default:
			ERROR("Unknown default-operation parameter for <edit-config>.");
			return (NULL);
			break;
		}
	}

	/* detect error-option parameter */
	if (error_option != 0) {
		switch (error_option) {
		case NC_EDIT_ERROPT_STOP:
			erropt = "stop-on-error";
			break;
		case NC_EDIT_ERROPT_CONT:
			erropt = "continue-on-error";
			break;
		case NC_EDIT_ERROPT_ROLLBACK:
			erropt = "rollback-on-error";
			break;
		default:
			ERROR("Unknown error-option parameter for <edit-config>.");
			return (NULL);
			break;
		}
	}

	/* detect test-option parameter */
	if (test_option != 0) {
		switch (test_option) {
		case NC_EDIT_TESTOPT_SET:
			testopt = "set";
			break;
		case NC_EDIT_TESTOPT_TEST:
			testopt = "test-only";
			break;
		case NC_EDIT_TESTOPT_TESTSET:
			testopt = "test-then-set";
			break;
		default:
			ERROR("Unknown test-option parameter for <edit-config>.");
			return (NULL);
			break;
		}
	}

	/* create edit-config envelope */
	if ((content = xmlNewNode(NULL, BAD_CAST "edit-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	/* set <target> element */
	node_target = xmlNewChild(content, ns, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		goto cleanup;
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		goto cleanup;
	}

	/* set <default-operation> element */
	if (default_operation != 0) {
		node_defop = xmlNewChild(content, ns, BAD_CAST "default-operation", BAD_CAST defop);
		if (node_defop == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	}

	/* set <error-option> element */
	if (error_option != 0) {
		if (xmlNewChild(content, ns, BAD_CAST "error-option", BAD_CAST erropt) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	}

	/* set <test-option> element */
	if (test_option != 0) {
		if (xmlNewChild(content, ns, BAD_CAST "test-option", BAD_CAST testopt) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	}

	if (source == NC_DATASTORE_CONFIG) {
		/* set <config> element */

		/* create empty config element in rpc request */
		if ((node_config = xmlNewChild(content, ns, BAD_CAST "config", NULL)) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
		if (config != NULL) {
			/* connect given configuration data with the rpc request */
			if (xmlAddChildList(node_config, xmlCopyNodeList(config)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				goto cleanup;
			}
		}
	} else if (source == NC_DATASTORE_URL) {
		/* set <url> instead of <config> */
		if (xmlNewChild(content, ns, BAD_CAST "url", BAD_CAST source_url) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			goto cleanup;
		}
	} else {
		ERROR("%s: unknown (or prohibited) source for <edit-config>.", __func__);
		goto cleanup;
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
cleanup:
	xmlFreeNode(content);
	return (rpc);
}

nc_rpc *ncxml_rpc_editconfig(NC_DATASTORE target, NC_DATASTORE source, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, NC_EDIT_TESTOPT_TYPE test_option, ...)
{
	xmlNodePtr config = NULL;
	const char* url = NULL;
	nc_rpc* retval;
	va_list argp;

	va_start(argp, test_option);
	switch (source) {
	case NC_DATASTORE_CONFIG:
		config = va_arg(argp, xmlNodePtr);
		break;
	case NC_DATASTORE_URL:
		url = va_arg(argp, const char*);
		break;
	default:
		ERROR("Unknown (or prohibited) source for <edit-config>.");
		va_end(argp);
		return (NULL);
		break;
	}

	retval = _rpc_editconfig(target, source, default_operation, error_option, test_option, config, url);
	va_end(argp);
	return(retval);
}

nc_rpc *nc_rpc_editconfig(NC_DATASTORE target, NC_DATASTORE source, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, NC_EDIT_TESTOPT_TYPE test_option, ...)
{
	xmlDocPtr config = NULL;
	const char* url = NULL, *config_s = NULL;
	char* config_S;
	nc_rpc* retval;
	va_list argp;

	va_start(argp, test_option);
	switch (source) {
	case NC_DATASTORE_CONFIG:
		config_s = va_arg(argp, const char*);
		break;
	case NC_DATASTORE_URL:
		url = va_arg(argp, const char*);
		break;
	default:
		ERROR("Unknown (or prohibited) source for <edit-config>.");
		va_end(argp);
		return (NULL);
		break;
	}

	/* transform string to the xmlNodePtr */
	/* add covering <config> element to allow to specify multiple root elements */
	if (asprintf(&config_S, "<config>%s</config>", config_s) == -1) {
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		va_end(argp);
		return (NULL);
	}

	/* prepare XML structure from given data */
	config = xmlReadMemory(config_S, strlen(config_S), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	free(config_S);
	if (config == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		va_end(argp);
		return (NULL);
	}

	retval = _rpc_editconfig(target, source, default_operation, error_option, test_option, config->children->children, url);
	xmlFreeDoc(config);
	va_end(argp);
	return(retval);
}

nc_rpc *nc_rpc_killsession(const char *kill_sid)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_sid;
	xmlNsPtr ns;

	/* check input parameter */
	if (kill_sid == NULL || strlen(kill_sid) == 0) {
		ERROR("Invalid session id for <kill-session> rpc message specified.");
		return (NULL);
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "kill-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	node_sid = xmlNewChild(content, ns, BAD_CAST "session-id", BAD_CAST kill_sid);
	if (node_sid == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_SESSION;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_getschema(const char* name, const char* version, const char* format)
{
	nc_rpc *rpc;
	xmlNodePtr content;
	xmlNsPtr ns;

	/* check mandatory input parameter */
	if (name == NULL) {
		ERROR("Invalid schema name specified.");
		return (NULL);
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "get-schema")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	ns = xmlNewNs(content, BAD_CAST NC_NS_MONITORING, NULL);
	xmlSetNs(content, ns);

	if (xmlNewChild(content, ns, BAD_CAST "identifier", BAD_CAST name) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if (version != NULL) {
		if (xmlNewChild(content, ns, BAD_CAST "version", BAD_CAST version) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (format != NULL) {
		if (xmlNewChild(content, ns, BAD_CAST "format", BAD_CAST format) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_READ;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_subscribe(const char* stream, const struct nc_filter *filter, const time_t* start, const time_t* stop)
{
	nc_rpc *rpc = NULL;
	xmlNodePtr content;
	xmlNsPtr ns;
	char* time;

	/* prepare notification namespace */


	if ((content = xmlNewNode(NULL, BAD_CAST "create-subscription")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	ns = xmlNewNs(content, BAD_CAST NC_NS_NOTIFICATIONS, NULL);
	xmlSetNs(content, ns);

	/* add <stream> specification if set */
	if (stream != NULL) {
		if (xmlNewChild(content, ns, BAD_CAST "stream", BAD_CAST stream) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* add filter specification if any required */
	if (process_filter_param(content, filter) != 0) {
		xmlFreeNode(content);
		return (NULL);
	}

	/* add <startTime> specification if set */
	if (start != NULL) {
		time = nc_time2datetime(*start);
		if (time == NULL || xmlNewChild(content, ns, BAD_CAST "startTime", BAD_CAST time) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			if (time != NULL) {
				free (time);
			}
			return (NULL);
		}
		free(time);
	}

	/* add <stopTime> specification if set */
	if (stop != NULL) {
		time = nc_time2datetime(*stop);
		if (time == NULL || xmlNewChild(content, ns, BAD_CAST "stopTime", BAD_CAST time) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			if (time != NULL) {
				free (time);
			}
			return (NULL);
		}
		free(time);
	}

	/* finnish the message building */
	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_SESSION;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_commit(void)
{
	nc_rpc *rpc;
	xmlNodePtr content;
	xmlNsPtr ns;

	if ((content = xmlNewNode(NULL, BAD_CAST "commit")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_discardchanges(void)
{
	nc_rpc *rpc;
	xmlNodePtr content;
	xmlNsPtr ns;

	if ((content = xmlNewNode(NULL, BAD_CAST "discard-changes")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	/* set namespace */
	ns = xmlNewNs(content, (xmlChar *) NC_NS_BASE10, NULL);
	xmlSetNs(content, ns);

	if ((rpc = nc_rpc_create(content)) != NULL) {
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	}
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *ncxml_rpc_generic(const xmlNodePtr data)
{
	nc_rpc *rpc;

	if (data == NULL) {
		ERROR("%s: parameter \'data\' can not be NULL.", __func__);
		return (NULL);
	}
	if ((rpc = nc_rpc_create(data)) != NULL) {
		rpc->type.rpc = NC_RPC_UNKNOWN;
	}

	return (rpc);
}

nc_rpc *nc_rpc_generic(const char* data)
{
	nc_rpc *rpc;
	xmlDocPtr doc_data;

	if (data == NULL) {
		ERROR("%s: parameter \'data\' can not be NULL.", __func__);
		return (NULL);
	}

	/* read data as XML document */
	doc_data = xmlReadMemory(data, strlen(data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (doc_data == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		return (NULL);
	}

	if ((rpc = nc_rpc_create(xmlDocGetRootElement(doc_data))) != NULL) {
		rpc->type.rpc = NC_RPC_UNKNOWN;
	}
	xmlFreeDoc(doc_data);

	return (rpc);
}
