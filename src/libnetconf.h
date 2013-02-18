/**
 * \file libnetconf.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf's main header.
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

#ifndef LIBNETCONF_H_
#define LIBNETCONF_H_

/**
 * \mainpage NETCONF library - libnetconf
 *
 * This documents provides documentation of the NETCONF library (libnetconf).
 *
 * CLIENT WORKFLOW
 * ---------------
 *
 * Here is a description of using libnetconf functions in a NETCONF client:
 * -# **Set verbosity (optional)**.\n
 * The verbosity of the libnetconf can be set by nc_verbosity(). By default,
 * libnetconf is completely silent.\n
 * There is a default message printing function writing messages on the stderr.
 * The application's specific message printing function can be set via
 * nc_callback_print() function.
 * -# **Set SSH authentication methods priorities (optional)**.\n
 * libnetconf supports several SSH authentication methods for connecting to
 * a NETCONF server over SSH. However, the used method is selected from a
 * list of supported authentication methods provided by the server. Client is
 * allowed to specify the priority of each supported authentication method via
 * nc_ssh_pref() function. The authentication method can also be disabled using
 * a negative priority value.\n
 * Default priorities are following:
 *    - *Interactive* (value 3)
 *    - *Password* (value 2)
 *    - *Public keys* (value 1)
 * -# **Set own callback(s) for the SSH authentication methods (optional)**.\n
 * User credentials are received via the callback functions specific for each
 * authentication method. There are default callbacks, but application can set
 * their own via:
 *    - *Interactive* - nc_callback_sshauth_interactive()
 *    - *Password* - nc_callback_sshauth_password()
 *    - *Publuc keys* - nc_callback_sshauth_passphrase(). Here can also
 *    the paths to the key files be specified by nc_set_publickey_path() and
 *    nc_set_privatekey_path(). If not set, libnetconf tries to find them in
 *    the default paths.
 * -# **Connect to the NETCONF server(s)**.\n
 * Simply call nc_session_connect() to connect to the specified host via SSH.
 * Authentication method is selected according to the default values or the previous
 * steps.
 * -# **Prepare NETCONF rpc message(s)**.\n
 * Creating NETCONF rpc messages is covered by the functions described in the section
 * \ref rpc. The application prepares NETCONF rpc messages according to the specified
 * attributes. These messages can be then repeatedly used for communication
 * over any of the created NETCONF sessions.
 * -# **Send the message to the selected NETCONF server**.\n
 * To send created NETCONF rpc message to the NETCONF server, use
 * nc_session_send_rpc() function. nc_session_send_recv() function connects
 * sending and receiving the reply (see the next step) into one blocking call.
 * -# **Get the server's rpc-reply message**.
 * When the NETCONF rpc is sent, use nc_session_recv_reply() to receive the
 * reply. To learn when the reply is coming, a file descriptor of the
 * communication channel can be checked by poll(), select(), ... This descriptor
 * can be obtained via nc_session_get_eventfd() function.
 * -# **Close the NETCONF session**.\n
 * When the communication is finished, the NETCONF session is closed by
 * nc_session_close() and all the used structures are freed by nc_session_free().
 * -# **Free all created objects**.\n
 * Do not forget to free created rpc messages (nc_rpc_free()),
 * \link nc_filter_new() filters\endlink (nc_filter_free()) or received NETCONF
 * rpc-replies (nc_reply_free()).
 *
 * SERVER ARCHITECTURE
 * -------------------
 *
 * It is [strongly] advised to set SUID (or SGID) bit on every application that is
 * built on libnetconf, as several internal functions behave based on this precondition.
 * libnetconf uses a number of files which pose a security risk if they are accessible
 * by untrustworthy users. This way it is possible not to restrict the use of
 * an application but only the access to its files, so keep this in mind when creating
 * any directories or files that are used. Generally, there are two basic approaches of how
 * to implement a NETCONF server using libnetconf.
 *
 * ###Single-level Architecture###
 *
 * ![Single-level architecture](../../img/sl_arch.png "Single-level architecture")
 *
 * In this case, all the server functionality is implemented as a single process.
 * It is started by SSH daemon as its Subsystem for each incoming NETCONF
 * session connection. The main issue of this approach is a simultaneous access
 * to shared resources. The device manager has to solve concurrent access to the
 * controlled device from its multiple instances. libnetconf itself has to deal
 * with simultaneous access to a shared configuration datastore.
 *
 * ###Multi-level Architecture###
 *
 * ![Multi-level architecture](../../img/ml_arch.png "Multi-level architecture")
 *
 * In the second case, there is only one device manager (NETCONF server) running
 * as a system daemon. This solves the problem of concurrent device access from
 * multiple processes. On the other hand, there is a need for inter-process
 * communication between the device manager and the agents launched as the SSH
 * Subsystems. These agents hold NETCONF sessions and receive requests from the
 * clients. libnetconf provides functions (nc_rpc_dump() and nc_rpc_build()) to
 * (de-)serialise content of the NETCONF messages. This allows the NETCONF messages
 * to be passed between an agent and a device manager that applies requests to
 * the operated device and a configuration datastore.
 *
 * SERVER WORKFLOW
 * ---------------
 *
 * Here is a description of using libnetconf functions in a NETCONF server.
 * According to the used architecture, the workflow can be split between an agent
 * and a server. For this purpose, functions nc_rpc_dump(), nc_rpc_build() and
 * nc_session_dummy() can be very helpful.
 *
 * 1. **Set the verbosity (optional)**.\n
 * The verbosity of the libnetconf can be set by nc_verbosity(). By default,
 * libnetconf is completely silent.\n
 * There is a default message printing function writing messages on the stderr.
 * On the server side, this is not very useful, since server usually runs
 * as a daemon without stderr. In this case, something like syslog should be
 * used. The application's specific message printing function can be set via
 * nc_callback_print() function.
 * 2. **Initiate datastore**.\n
 * As the first step, create a datastore handle using ncds_new() with the specific
 * datastore type implementation. Optionally, some implementation-type-specific
 * parameters can be set (e.g. ncds_file_set_path()). Finally, init the datastore
 * using ncds_init() that returns datastore's ID which is used in subsequent
 * calls.
 * 3. **Accept incoming NETCONF connection**.\n
 * This is done by a single call of nc_session_accept(). Optionally, any specific
 * capabilities supported by the server can be set.
 * 4. **Process incoming requests**.\n
 * Use nc_session_recv_rpc() to get the next request from the client from the
 * specified NETCONF session. In case of an error return code, the state of the
 * session should be checked by nc_session_get_status() to learn if the
 * session can be further used.\n
 * According to the type of the request (nc_rpc_get_type()), perform appropriate
 * action:
 *    - *NC_RPC_DATASTORE_READ* or *NC_RPC_DATASTORE_WRITE*: use ncds_apply_rpc()
 *    to perform the requested operation on the datastore. If the request affects
 *    the running datastore (nc_rpc_get_target() == NC_DATASTORE_RUNNING),
 *    apply configuration changes to the controlled device.
 *    - *NC_RPC_SESSION*: \todo some common function to perform this type of
 *    requests will be added.\n
 *
 * 5. **Reply to the client's request**.\n
 * The reply message is automatically generated by the ncds_apply_rpc() function.
 * However, server can generate its own replies using nc_reply_ok(),
 * nc_reply_data() or nc_reply_error() functions. The reply is sent to the
 * client using nc_session_send_reply() call.
 * 6. **Free all unused objects**.\n
 * Do not forget to free received rpc messages (nc_rpc_free()) and any created
 * replies (nc_reply_free()).
 * 7. **Server loop**.\n
 * Repeat previous three steps.
 * 8. **Close the NETCONF session**.\n
 * Use functions nc_session_close() and nc_session_free() (in this order) to
 * close and free all the used sources and structures connected with the session.
 * Session can be closed by the server based on its internal reasons or by
 * the libnetconf due to some error. In the second case, libnetconf marks the
 * status of the session as non-working (nc_session_get_status !=
 * NC_SESSION_STATUS_WORKING).
 *
 * GLOSSARY
 * --------
 *
 * - **message** - all the types of messages passing through NETCONF. It
 * includes rpc, rpc-reply and notification.
 */

/**
 * \defgroup rpc NETCONF rpc
 * \brief libnetconf's functions for handling NETCONF \<rpc\> messages.
 */

/**
 * \defgroup reply NETCONF rpc-reply
 * \brief libnetconf's functions for handling NETCONF \<rpc-reply\> messages.
 */

/**
 * \defgroup session NETCONF Session
 * \brief libnetconf's functions for handling NETCONF sessions.
 */

/**
 * \defgroup store Datastore operations
 * \brief libnetconf's functions for handling NETCONF datastores.
 */

/**
 * \defgroup genAPI General functions
 * \brief libnetconf's miscellaneous functions.
 */

/**
 * \defgroup withdefaults With-defaults capability
 * \brief libnetconf's implementation of NETCONF with-defaults capability as
 * defined in RFC 6243.
 */

/**
 * \defgroup notifications NETCONF Event Notifications
 * \brief libnetconf's implementation of NETCONF asynchronous message delivery
 * as defined in RFC 5277.
 */

/**
 * \internal
 * \defgroup internalAPI Internal API
 * \brief libnetconf's functions, structures and macros for internal usage.
 */

#include "config.h"

#include "netconf.h"
#include "callbacks.h"
#include "session.h"
#include "messages.h"
#include "error.h"
#include "datastore.h"
#include "with_defaults.h"

#ifndef DISABLE_NOTIFICATIONS
#  include "notifications.h"
#endif

#endif /* LIBNETCONF_H_ */
