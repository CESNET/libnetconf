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
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "messages.h"
#include "netconf_internal.h"

struct nc_filter *nc_filter_new(NC_FILTER_TYPE type, char* filter)
{
	struct nc_filter *retval;

	retval = malloc(sizeof(struct nc_filter));
	if (retval == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	switch (type) {
	case NC_FILTER_SUBTREE:
		retval->type_string = strdup("subtree");
		break;
	default:
		ERROR("nc_filter_new: Invalid filter type specified.");
		free(retval);
		return (NULL);
	}
	retval->type = type;
	retval->content = strdup(filter);

	return (retval);
}

void nc_filter_free(struct nc_filter *filter)
{
	if (filter != NULL) {
		if (filter->content != NULL) {
			free(filter->content);
		}
		if (filter->type_string != NULL) {
			free(filter->type_string);
		}
		free(filter);
	}
}

char* nc_reply_get_string(const nc_reply *reply)
{
	xmlChar *buf;
	int len;

	if (reply == NULL || reply->doc == NULL) {
		ERROR("nc_reply_get_string: invalid input parameter.");
		return (NULL);
	}

	xmlDocDumpFormatMemory(reply->doc, &buf, &len, 1);
	return ((char*) buf);
}

nc_msgid nc_reply_get_msgid(const nc_reply *reply)
{
	if (reply != NULL) {
		return (reply->msgid);
	} else {
		return (0);
	}
}

NC_REPLY_TYPE nc_reply_get_type(const nc_reply *reply)
{
	if (reply != NULL) {
		return (reply->type);
	} else {
		return (NC_REPLY_UNKNOWN);
	}
}

char *nc_reply_get_data(const nc_reply *reply)
{
	xmlDocPtr doc;
	xmlNodePtr node, root;
	xmlChar *buf;
	int len;

	if (reply == NULL || reply->type != NC_REPLY_DATA || reply->doc == NULL || reply->doc->children == NULL || /* <rpc-reply> */
	reply->doc->children->children == NULL || /* <data> */
	reply->doc->children->children->children == NULL) { /* content */
		ERROR("nc_reply_get_data: invalid input parameter.");
		return (NULL);
	}

	if ((doc = xmlNewDoc(BAD_CAST XML_VERSION)) == NULL) {
		ERROR("nc_reply_get_data: xmlNewDoc failed.");
		return (NULL);
	}
	doc->encoding = xmlStrdup(BAD_CAST UTF8);
	xmlDocSetRootElement(doc, root = xmlCopyNode(node = reply->doc->children->children->children, 1));
	for (node = node->next; node != NULL; node = node->next) {
		xmlAddNextSibling(root, xmlCopyNode(node, 1));
	}
	xmlDocDumpFormatMemory(doc, &buf, &len, 1);
	xmlFreeDoc(doc);

	return ((char*) buf);
}

char *nc_reply_get_errormsg(const nc_reply *reply)
{
	xmlXPathContextPtr ctxt = NULL;
	xmlXPathObjectPtr xpath_obj = NULL;
	const xmlChar *ns = NULL;
	char *nsid = NULL;
	char *xpath_query;
	char *retval;

	if (reply == NULL || reply->type != NC_REPLY_ERROR) {
		ERROR("nc_reply_get_errormsg: invalid input parameter.");
		return (NULL);
	}

	/* create xpath evaluation context of the xml  document */
	if ((ctxt = xmlXPathNewContext(reply->doc)) == NULL) {
		ERROR("Unable to create XPath context for the <rpc-reply> message (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	if (reply->doc->children->ns != NULL) {
		ns = reply->doc->children->ns->href;
		/* register namespace for the context of internal configuration file */
		if (xmlXPathRegisterNs(ctxt, BAD_CAST "base", ns) != 0) {
			ERROR("Unable to register namespace for <rpc-reply> message (%s:%d).", __FILE__, __LINE__);
			xmlXPathFreeContext(ctxt);
			return (NULL);
		}
		nsid = "base:";
	} else {
		nsid = "";
	}
	/* get the device module subtree */
	if (asprintf(&xpath_query, "//%srpc-error/%serror-message", nsid, nsid) == -1) {
		ERROR("Unable to prepare XPath query for <error-message> (%s:%d).", __FILE__, __LINE__);
		xmlXPathFreeContext(ctxt);
		return (NULL);
	}
	if ((xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath_query, ctxt)) == NULL) {
		ERROR("XPath query evaluation failed (%s:%d).", __FILE__, __LINE__);
		xmlXPathFreeContext(ctxt);
		free(xpath_query);
		return (NULL);
	}
	free(xpath_query);

	if (xpath_obj->nodesetval->nodeNr != 1) {
		WARN("Missing <error-message> in <rpc-error>.");
		xmlXPathFreeObject(xpath_obj);
		xmlXPathFreeContext(ctxt);
		return (NULL);
	}
	retval = (char*) xmlNodeGetContent(xpath_obj->nodesetval->nodeTab[0]);

	/* cleanup */
	xmlXPathFreeObject(xpath_obj);
	xpath_obj = NULL;
	xmlXPathFreeContext(ctxt);
	ctxt = NULL;

	return (retval);
}

nc_rpc *nc_msg_client_hello(char **cpblts)
{
	nc_rpc *msg;
	xmlNodePtr node;
	int i;

	if (cpblts == NULL || cpblts[0] == NULL) {
		ERROR("hello: no capability specified");
		return (NULL);
	}

	msg = malloc(sizeof(nc_rpc));
	if (msg == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	msg->doc = xmlNewDoc(BAD_CAST "1.0");
	msg->doc->encoding = xmlStrdup(BAD_CAST UTF8);

	/* create root element */
	msg->doc->children = xmlNewDocNode(msg->doc, NULL, BAD_CAST NC_HELLO_MSG, NULL);

	/* set namespace */
	xmlNewNs(msg->doc->children, (xmlChar *) NC_NS_BASE10, NULL);

	/* create capabilities node */
	node = xmlNewChild(msg->doc->children, NULL, BAD_CAST "capabilities", NULL);
	for (i = 0; cpblts[i] != NULL; i++) {
		xmlNewChild(node, NULL, BAD_CAST "capability", BAD_CAST cpblts[i]);
	}

	return (msg);
}

void nc_msg_free(struct nc_msg *msg)
{
	if (msg != NULL) {
		xmlFreeDoc(msg->doc);
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

	if (msg == NULL || msg->doc == NULL) {
		return (NULL);
	}

	dupmsg = malloc(sizeof(struct nc_msg));
	dupmsg->doc = xmlCopyDoc(msg->doc, 1);
	dupmsg->msgid = msg->msgid;
	dupmsg->type = msg->type;

	return (dupmsg);
}

nc_rpc *nc_msg_server_hello(char **cpblts, char* session_id)
{
	nc_rpc *msg;

	msg = nc_msg_client_hello(cpblts);
	if (msg == NULL) {
		return (NULL);
	}

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
 * @brief Create \<rpc\> envelope and insert given data
 *
 * @param[in]	content		pointer to xml node containing data
 *
 * @return Prepared nc_rpc structure.
 */
nc_rpc* nc_rpc_create(xmlNodePtr content)
{
	nc_rpc* rpc;

	xmlDocPtr rpcmsg = NULL;

	if ((rpcmsg = xmlNewDoc(BAD_CAST XML_VERSION)) == NULL) {
		ERROR("xmlNewDoc failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return NULL;
	}
	rpcmsg->encoding = xmlStrdup(BAD_CAST UTF8);

	if ((rpcmsg->children = xmlNewDocNode(rpcmsg, NULL, BAD_CAST "rpc", NULL)) == NULL) {
		ERROR("xmlNewDocNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(rpcmsg);
		return NULL;
	}

	if (xmlNewProp(rpcmsg->children, BAD_CAST "message-id", BAD_CAST "") == NULL) {
		ERROR("xmlNewProp failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(rpcmsg);
		return NULL;
	}

	if (xmlAddChild(rpcmsg->children, xmlCopyNode(content, 1)) == NULL) {
		ERROR("xmlAddChild failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(rpcmsg);
		return NULL;
	}

	rpc = malloc(sizeof(nc_rpc));
	if (rpc == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	rpc->doc = rpcmsg;

	return (rpc);
}

nc_rpc *nc_rpc_closesession()
{
	nc_rpc *rpc;
	xmlNodePtr content;

	if ((content = xmlNewNode(NULL, BAD_CAST "close-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_getconfig(NC_DATASTORE_TYPE source, struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlDocPtr doc_filter = NULL;
	xmlNodePtr content, node_source, node_filter = NULL;
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
	node_source = xmlNewChild(content, NULL, BAD_CAST "source", NULL);
	if (node_source == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_source, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	if (filter != NULL) {
		if (filter->type == NC_FILTER_SUBTREE) {
			doc_filter = xmlReadMemory(filter->content, strlen(filter->content), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			if (doc_filter == NULL) {
				ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}

			node_filter = xmlNewChild(content, NULL, BAD_CAST "filter", NULL);
			if (node_filter == NULL) {
				ERROR("xmlCopyNode failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				xmlFreeDoc(doc_filter);
				return (NULL);
			}
			if (xmlAddChild(node_filter, xmlCopyNode(doc_filter->children, 1)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				xmlFreeDoc(doc_filter);
				return (NULL);
			}
		}

		if (doc_filter != NULL && node_filter != NULL) {
			xmlFreeDoc(doc_filter);
			if (xmlNewProp(node_filter, BAD_CAST "type", BAD_CAST filter->type_string) == NULL) {
				ERROR("xmlNewProp failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		} else {
			WARN("Unknown filter type used - skipping filter.");
		}
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_get(struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlDocPtr doc_filter = NULL;
	xmlNodePtr content, node_filter = NULL;

	if ((content = xmlNewNode(NULL, BAD_CAST "get")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	if (filter != NULL) {
		if (filter->type == NC_FILTER_SUBTREE) {
			doc_filter = xmlReadMemory(filter->content, strlen(filter->content), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
			if (doc_filter == NULL) {
				ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}

			node_filter = xmlNewChild(content, NULL, BAD_CAST "filter", NULL);
			if (node_filter == NULL) {
				ERROR("xmlCopyNode failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				xmlFreeDoc(doc_filter);
				return (NULL);
			}
			if (xmlAddChild(node_filter, xmlCopyNode(doc_filter->children, 1)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				xmlFreeDoc(doc_filter);
				return (NULL);
			}
		}

		if (doc_filter != NULL && node_filter != NULL) {
			xmlFreeDoc(doc_filter);
			if (xmlNewProp(node_filter, BAD_CAST "type", BAD_CAST filter->type_string) == NULL) {
				ERROR("xmlNewProp failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeNode(content);
				return (NULL);
			}
		} else {
			WARN("Unknown filter type used - skipping filter.");
		}
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);

}

nc_rpc *nc_rpc_deleteconfig(NC_DATASTORE_TYPE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	char* datastore;

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
	default:
		ERROR("Unknown target datastore for <delete-config>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "delete-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_lock(NC_DATASTORE_TYPE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
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

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_unlock(NC_DATASTORE_TYPE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
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

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_copyconfig(NC_DATASTORE_TYPE source, NC_DATASTORE_TYPE target, const char *data)
{
	nc_rpc *rpc;
	xmlDocPtr doc_data;
	xmlNodePtr content, node_target, node_source, config;
	NC_DATASTORE_TYPE params[2] = {source, target};
	char *datastores[2]; /* 0 - source, 1 - target */
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
		case NC_DATASTORE_NONE:
			if (i == 0) {
				if (data != NULL) {
					/* source configuration data are specified as given data */
					datastores[i] = NULL;
				} else {
					ERROR("Missing source configuration data for <copy-config>.");
					return (NULL);
				}
			} else {
				ERROR("Unknown target datastore for <copy-config>.");
				return (NULL);
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

	/* <source> */
	node_source = xmlNewChild(content, NULL, BAD_CAST "source", NULL);
	if (node_source == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (datastores[0] == NULL) {
		/* source configuration is given as data parameter */

		/* prepare covering element in rpc request */
		if ((config = xmlNewChild(node_source, NULL, BAD_CAST "config", NULL)) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}

		/* prepare XML structure from given data */
		doc_data = xmlReadMemory(data, strlen(data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		if (doc_data == NULL) {
			ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}

		/* connect given configuration data with the rpc request */
		if (xmlAddChild(config, xmlCopyNode(doc_data->children, 1)) == NULL) {
			ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			xmlFreeDoc(doc_data);
			return (NULL);
		}

		/* free no more needed structure */
		xmlFreeDoc(doc_data);
	} else {
		/* source is one of the standard datastores */
		if (xmlNewChild(node_source, NULL, BAD_CAST datastores[0], NULL) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* <target> */
	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastores[1], NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_editconfig(NC_DATASTORE_TYPE target, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, const char *data)
{
	nc_rpc *rpc;
	xmlDocPtr doc_data;
	xmlNodePtr content, node_target, node_defop, node_erropt, node_config;
	char* datastore, *defop, *erropt;

	if (data == NULL || strlen(data) == 0) {
		ERROR("Invalid configuration data for <edit-config>");
		return (NULL);
	}

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

	/* create edit-config envelope */
	if ((content = xmlNewNode(NULL, BAD_CAST "edit-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* set <target> element */
	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* set <default-operation> element */
	if (default_operation != 0) {
		node_defop = xmlNewChild(content, NULL, BAD_CAST "default-operation", BAD_CAST defop);
		if (node_defop == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* set <error-option> element */
	if (error_option != 0) {
		node_erropt = xmlNewChild(content, NULL, BAD_CAST "error-option", BAD_CAST erropt);
		if (node_erropt == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* set <config> element */
	/* prepare covering element in rpc request */
	if ((node_config = xmlNewChild(content, NULL, BAD_CAST "config", NULL)) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* prepare XML structure from given data */
	doc_data = xmlReadMemory(data, strlen(data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (doc_data == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* connect given configuration data with the rpc request */
	if (xmlAddChild(node_config, xmlCopyNode(doc_data->children, 1)) == NULL) {
		ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		xmlFreeDoc(doc_data);
		return (NULL);
	}
	xmlFreeDoc(doc_data);

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}


nc_rpc *nc_rpc_killsession(const char *kill_sid)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_sid;

	/* check input parameter */
	if (kill_sid == NULL || strlen(kill_sid) == 0) {
		ERROR("Invalid session id for <kill-session> rpc message specified.");
		return (NULL);
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "kill-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_sid = xmlNewChild(content, NULL, BAD_CAST "session-id", BAD_CAST kill_sid);
	if (node_sid == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	xmlFreeNode(content);

	return (rpc);
}
