/*
 * netconf-server
 * Author Radek Krejci <rkrejci@cesnet.cz>
 *
 * Example implementation of event-driven NETCONF server using libnetconf.
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
#include <stdio.h>
#include <syslog.h>
#include <event2/event.h>

#include "../../src/libnetconf.h"

#define VERSION "0.1"

struct srv_config {
	struct nc_session *session;
	ncds_id dsid;
	struct event_base *event_base;
	struct event *event_input;
};

int clb_print(const char* msg)
{
	syslog(LOG_CRIT, msg);
	return (EXIT_SUCCESS);
}

void print_version()
{
	fprintf(stdout, "libnetconf server version: %s\n", VERSION);
	fprintf(stdout, "compile time: %s, %s\n", __DATE__, __TIME__);
}

void process_rpc(evutil_socket_t in, short events, void *arg)
{
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	NC_RPC_TYPE req_type;
	NC_OP req_op;
	struct srv_config *config = (struct srv_config*)arg;

	/* receive incoming message */
	if (nc_session_recv_rpc(config->session, &rpc) == 0) {
		/* something really bad happend, and communication os not possible anymore */
		nc_session_free(config->session);
		event_base_loopbreak(config->event_base);
		return;
	}

	/* process it */
	req_type = nc_rpc_get_type(rpc);
	req_op = nc_rpc_get_op(rpc);
	if (req_type == NC_RPC_SESSION) {
		/* process operations affectinf session */
		if (req_op == NC_OP_CLOSESESSION) {
			/* exit the event loop immediately without processing any following request */
			event_base_loopbreak(config->event_base);
		} else if (req_op == NC_OP_KILLSESSION) {
			/* todo: kill the requested session */
			reply = nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED));
		}
	} else if (req_type == NC_RPC_DATASTORE_READ) {
		/* process operations reading datastore */
		if (req_op == NC_OP_GETCONFIG) {
			reply = nc_reply_data("<libnetconf-server xmlns=\"urn:cesnet:tmc:libnetconf-server:0.1\"/>");
		} else if (req_op == NC_OP_GET) {
			reply = nc_reply_data("<libnetconf-server xmlns=\"urn:cesnet:tmc:libnetconf-server:0.1\"><version>" VERSION "</version></libnetconf-server>");
		}
	} else if (req_type == NC_RPC_DATASTORE_WRITE) {
		/* process operations affecting datastore */
		switch (req_op) {
		case NC_OP_LOCK:
		case NC_OP_UNLOCK:
			reply = ncds_apply_rpc(config->dsid, config->session, rpc);
			break;
		default:
			reply = nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED));
			break;
		}
	} else {
		/* process other operations */
	}

	/* create reply */
	if (reply == NULL) {
		/*
		 * Following operations are handled by this answer:
		 * - edit-config
		 * - copy-config
		 * - delete-config
		 * - lock, unlock
		 * Mentioned operations will be handled correctly in the future,
		 * for now, server does not work with any real datastore.
		 */
		reply = nc_reply_ok();
	}

	/* and send the reply to the client */
	nc_session_send_reply(config->session, rpc, reply);

	/* and run again when a next message comes */
}

int main(int argc, char *argv[])
{
	struct srv_config config;
	struct ncds_ds* datastore;

	/* set verbosity and function to print libnetconf's messages */
	nc_verbosity(NC_VERB_DEBUG);

	/* set message printing into the system log */
	openlog("ncserver", LOG_PID, LOG_DAEMON);
	nc_callback_print(clb_print);

	/* prepare configuration datastore */
	datastore = ncds_new(NCDS_TYPE_FILE, "/tmp/model.yin");
	if (datastore == NULL) {
		clb_print("Datastore preparing failed.");
		return (EXIT_FAILURE);
	}
	if (ncds_file_set_path(datastore, "/tmp/datastore.xml") != 0) {
		clb_print("Linking datastore to a file failed.");
		return (EXIT_FAILURE);
	}
	config.dsid = ncds_init(datastore);
	if (config.dsid <= 0) {
		clb_print("Initiating datastore failed.");
		return (EXIT_FAILURE);
	}

	/* create the NETCONF session */
	config.session = nc_session_accept(NULL);
	if (config.session == NULL) {
		clb_print("Session not established.\n");
		return (EXIT_FAILURE);
	}

	/* prepare event base (libevent) */
	config.event_base = event_base_new();
	if (config.event_base == NULL) {
		clb_print("Event base initialisation failed.\n");
		return (EXIT_FAILURE);
	}

	/* create the event of receiving incoming message from the NETCONF client */
	config.event_input = event_new(config.event_base, (evutil_socket_t)nc_session_get_eventfd(config.session), EV_READ | EV_PERSIST, process_rpc, (void*) (&config));

	/* add the event to the event base and run the main event loop */
	event_add (config.event_input, NULL);
	event_base_dispatch(config.event_base);

	/* cleanup */
	event_free(config.event_input);
	event_base_free(config.event_base);

	/* bye, bye */
	return (EXIT_SUCCESS);
}
