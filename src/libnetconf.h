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
 * \mainpage About
 *
 * [netconfwg]: http://trac.tools.ietf.org/wg/netconf/trac/wiki "NETCONF WG"
 * [mlist]: https://groups.google.com/forum/#!forum/libnetconf
 * [issues]: https://code.google.com/p/libnetconf/issues/list "Issues"
 * [TMC]: https://www.liberouter.org/ "Tools for Monitoring and Configuration"
 * [CESNET]: http://www.ces.net/ "CESNET"
 * [RFC6241]: http://tools.ietf.org/html/rfc6241 "RFC 6241"
 * [wrunning]: http://tools.ietf.org/html/rfc6241#section-8.2
 * [candidate]: http://tools.ietf.org/html/rfc6241#section-8.3
 * [validate]: http://tools.ietf.org/html/rfc6241#section-8.6
 * [startup]: http://tools.ietf.org/html/rfc6241#section-8.7
 * [url]: http://tools.ietf.org/html/rfc6241#section-8.8
 * [RFC6242]: http://tools.ietf.org/html/rfc6242 "RFC 6242"
 * [RFC5277]: http://tools.ietf.org/html/rfc5277 "RFC 5277"
 * [RFC6470]: http://tools.ietf.org/html/rfc6470 "RFC 6470"
 * [RFC6243]: http://tools.ietf.org/html/rfc6243 "RFC 6243"
 * [RFC6536]: http://tools.ietf.org/html/rfc6536 "RFC 6536"
 * [interopevent]: http://www.internetsociety.org/articles/successful-netconf-interoperability-testing-announced-ietf-85
 *
 * libnetconf is a NETCONF library in C intended for building NETCONF clients
 * and servers. It provides basic functions to connect NETCONF client and server
 * to each other via SSH, to send and receive NETCONF messages and to store and
 * work with the configuration data in a datastore.
 *
 * libnetconf implements the NETCONF protocol introduced by IETF. More
 * information about NETCONF protocol can be found at [NETCONF WG][netconfwg].
 *
 * libnetconf is currently under development at the [TMC] department of [CESNET].
 * Any testing of the library is welcome. Please inform us about your
 * experiences with using libnetconf via our [mailing list][mlist] or the
 * [Google Code's Issue section][issues]. Any feature suggestion or bugreport
 * is also appreciated.
 *
 * In November 2012, CESNET attended the NETCONF Interoperability Event held in
 * Atlanta, prior to the IETF 85 meeting. We went to the event with the
 * libnetconf based client and server and successfully tested interoperability
 * with other implementations. The notes from the event can be found
 * [here][interopevent].
 *
 * ### Features ###
 *
 * - NETCONF v1.0 and v1.1 compliant ([RFC 6241][RFC6241])
 * - NETCONF over SSH ([RFC 6242][RFC6242]) including Chunked Framing Mechanism
 * - NETCONF Writable-running capability ([RFC 6241][wrunning])
 * - NETCONF Candidate configuration capability ([RFC 6241][candidate])
 * - NETCONF Validate capability ([RFC 6241][validate])
 * - NETCONF Distinct startup capability ([RFC 6241][startup])
 * - NETCONF URL capability ([RFC 6241][url])
 * - NETCONF Event Notifications ([RFC 5277][RFC5277] and [RFC 6470][RFC6470])
 * - NETCONF With-defaults capability ([RFC 6243][RFC6243])
 * - NETCONF Access Control ([RFC 6536][RFC6536])
 * - NETCONF CLI client for GNU/Linux
 *
 * ### BSD License ###
 *
 * Copyright (C) 2012-2013 CESNET, z.s.p.o.
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
 * This software is provided "as is", and any express or implied
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
 *
 * ![CESNET, z.s.p.o.](../../img/cesnet-logo-125.png)
 *
 */

/**
 * \page install Compilation and Installation
 *
 * ## Cloning git repository ##
 *
 * As written on libnetconfs [Google Code page](https://code.google.com/p/libnetconf/source/checkout)
 * the repository can be cloned using following command:
 *
 * ~~~~~~~
 * $ git clone https://code.google.com/p/libnetconf/
 * ~~~~~~~
 *
 * \note In case that git fails to clone the repository make sure git is at least version 1.6.6.
 *
 * ## Compilation ##
 *
 * libnetconf uses standard GNU Autotools toolchain. To compile and install
 * libnetconf you have to go through the following three steps:
 *
 * ~~~~~~~~~~~~~~
 * $ ./configure
 * $ make
 * # make install
 * ~~~~~~~~~~~~~~
 *
 * This way the library will be installed in `/usr/local/lib/` (or lib64) and
 * `/usr/local/include/` respectively.
 *
 * ### Configure Options ###
 *
 * `configure` script supports the following options. The full list of the
 * accepted options can be shown by `--help` option.
 *
 * - `--disable--ssh2`
 *  - Remove dependency on the `libssh2` library. By default,
 *    the `libssh2` library is used by the client side functions to create SSH
 *    connection to a remote host. If the usage of the `libssh2` is disabled,
 *    libnetconf will use a standalone `ssh(1)` client located in a system path.
 *    `ssh(1)` client is, for example, part of the OpenSSH.
 *
 * - `--disable-notifications`
 *  - Remove support for the NETCONF Notifications. As a side effect, D-Bus
 *    (libdbus) dependency is also removed.
 *
 * - `--with-nacm-recovery-uid=<uid>` \anchor configure-nacm-recovery
 *  - Specify user ID to be used for the identification of the \ref
 *  nacm-recovery "NACM Recovery Session".
 *
 * - `--enable-debug`
 *  - Add debugging information for a debugger.
 *
 * - `--with-suid=<user>`
 *  - Limit usage of the libnetconf to the specific _user_. With this option,
 *    libnetconf creates shared files and other resources with access rights
 *    limited to the specified _user_. This option can be freely combined with
 *    the `--with-sgid` option. If neither `--with-suid` nor `--with-sgid`
 *    option is specified, full access rights for all users are granted.
 *
 * - `--with-sgid=<group>`
 *  - Limit usage of the libnetconf to the specific _group_. With this option,
 *    libnetconf creates shared files and other resources with access rights
 *    limited to the specified _group_. This option can be freely combined with
 *    the `--with-suid` option. If neither `--with-suid` nor `--with-sgid`
 *    option is specified, full access rights for all users are granted.
 *
 * \note
 * If the library is built with `--with-suid` or `--with-sgid` options,
 * the proper suid or/and sgid bit should be properly set to the server-side
 * application binaries using the libnetconf library. An example of such
 * settings can be seen on the example server application.
 *
 * - `--with-examples`
 *  - Besides the libnetconf library, build also example applications located
 *    in the `examples/` subdirectory. Examples include NETCONF client, NETCONF
 *    server and NETCONF Notifications reader.
 *
 */

/**
 * \page usage Using libnetconf
 *
 * Useful notes on using the libnetconf library can be found in the following
 * articles. However, the source codes of the example client and server are
 * supposed to be the most accurate, up-to-date and generally the best source of
 * information.
 *
 * - \subpage client
 * - \subpage server
 *
 * GLOSSARY
 * --------
 *
 * - **message** - all the types of messages passing through NETCONF. It
 * includes rpc, rpc-reply and notification.
 *
 */

/**
 * \page client Client
 *
 * Client Workflow
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
 */

/**
 * \page server Server
 *
 * Server Architecture
 * -------------------
 *
 * It is [strongly] advised to set SUID (or SGID) bit on every application that is
 * built on libnetconf for a user (or group) created for this purpose, as several
 * internal functions behave based on this precondition. libnetconf uses a number
 * of files which pose a security risk if they are accessible by untrustworthy users.
 * This way it is possible not to restrict the use of an application but only the
 * access to its files, so keep this in mind when creating any directories or files
 * that are used.
 *
 * Generally, there are two basic approaches of how to implement
 * a NETCONF server using libnetconf.
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
 * Server Workflow
 * ---------------
 *
 * Here is a description of using libnetconf functions in a NETCONF server.
 * According to the used architecture, the workflow can be split between an agent
 * and a server. For this purpose, functions nc_rpc_dump(), nc_rpc_build() and
 * nc_session_dummy() can be very helpful.
 *
 * 1. **Set the verbosity** (optional).\n
 * The verbosity of the libnetconf can be set by nc_verbosity(). By default,
 * libnetconf is completely silent.\n
 * There is a default message printing function writing messages on the stderr.
 * On the server side, this is not very useful, since server usually runs
 * as a daemon without stderr. In this case, something like syslog should be
 * used. The application's specific message printing function can be set via
 * nc_callback_print() function.
 * 2. **Initiate libnetconf**\n
 * As the first step, libnetconf MUST be initiated using nc_init(). At this
 * moment, the libnetconf subsystems, such as NETCONF Notifications or NETCONF
 * Access Control, are initiated according to specified parameter of the
 * nc_init() function.
 * 3. **Set With-defaults basic mode** (optional)\n
 * By default, libnetconf uses _explicit_ basic mode of the with-defaults
 * capability. The basic mode can be changed via ncdflt_set_basic_mode()
 * function. libnetconf supports _explicit_, _trim_, _report-all_ and
 * _report-all-tagged_ basic modes of the with-defaults capability.
 * 4. **Initiate datastore**.\n
 * Now, a NETCONF datastore(s) can be created. Each libnetconf's datastore
 * is connected with a single configuration data model. This connection is
 * defined by calling ncds_new() function, which returns the datastore handler
 * for further manipulation with an uninitialized datastore. Usign this function,
 * caller also specifies which datastore implementation type will be used.
 * Optionally, some implementation-type-specific parameters can be set (e.g.
 * ncds_file_set_path()). Finally, datastore must be initiated by ncds_init()
 * that returns datastore's ID which is used in subsequent calls. There is a set
 * of special implicit datastores with ID #NCDS_INTERNAL_ID, that refers
 * to the libnetconf's internal datastore(s).\n
 * Optionally, each datastore can be extended by an augment data model that can
 * be specified by ncds_add_model(). The same function can be used to specify
 * models to resolve YANG's `import` statements. Alternatively, using
 * ncds_add_models_path(), caller can specify a directory where such models can
 * be found automatically. libnetconf searches for the needed models
 * based on the modules names. Filename of the model is expected in a form
 * `module_name[@revision].yin`.\n
 * Caller can also switch on or off the YANG `feauters` in the specific module
 * using ncds_feature_enable(), ncds_feature_disable(), ncds_features_enableall()
 * and ncds_features_disableall() functions.\n
 * Finally, ncds_consolidate() must be called to check all internal structures
 * and to solve all `import`, `uses` and `augment` statements.
 * 5. **Initiate controlled device**\n
 * This step is actually out of the libnetconf scope. From the NETCONF point
 * of view, startup configuration data should be applied to the running data
 * store at this point. ncds_device_init() can be used to perform this task,
 * but applying running configuration data to the controlled device must be done
 * by a server specific (non-libnetconf) function.
 * 6. **Accept incoming NETCONF connection**.\n
 * This is done by a single call of nc_session_accept(). Optionally, any specific
 * capabilities supported by the server can be set as the function's parameter.
 * 7. **Process incoming requests**.\n
 * Use nc_session_recv_rpc() to get the next request from the client from the
 * specified NETCONF session. In case of an error return code, the state of the
 * session should be checked by nc_session_get_status() to learn if the
 * session can be further used.\n
 * According to the type of the request (nc_rpc_get_type()), perform appropriate
 * action:
 *    - *NC_RPC_DATASTORE_READ* or *NC_RPC_DATASTORE_WRITE*: use ncds_apply_rpc()
 *    to perform the requested operation on the datastore. If the request affects
 *    the running datastore (nc_rpc_get_target() == NC_DATASTORE_RUNNING),
 *    apply configuration changes to the controlled device. ncds_apply_rpc()
 *    applies the request to the specified datastore. Besides the datastores
 *    created explicitely by the ncds_new() and ncds_init() calls, remember to
 *    apply the request to the internal libnetconf datastore with ID 0. To merge
 *    results of the separate ncds_apply_rpc() calls can be merged by
 *    nc_reply_merge() into a single reply message.
 *    - *NC_RPC_SESSION*: See example server source codes. Some common function
 *    will be added in a future to handle these requests.
 * 8. **Reply to the client's request**.\n
 * The reply message is automatically generated by the ncds_apply_rpc() function.
 * However, server can generate its own replies using nc_reply_ok(),
 * nc_reply_data() or nc_reply_error() functions. The reply is sent to the
 * client using nc_session_send_reply() call.
 * 9. **Free all unused objects**.\n
 * Do not forget to free received rpc messages (nc_rpc_free()) and any created
 * replies (nc_reply_free()).
 * 10. **Server loop**.\n
 * Repeat previous three steps.
 * 11. **Close the NETCONF session**.\n
 * Use functions nc_session_close() and nc_session_free() (in this order) to
 * close and free all the used sources and structures connected with the session.
 * Session can be closed by the server based on its internal reasons or by
 * the libnetconf due to some error. In the second case, libnetconf marks the
 * status of the session as non-working (nc_session_get_status !=
 * NC_SESSION_STATUS_WORKING).
 * 12. **Close the libnetconf instance**\n
 * Close internal libnetconf structures and subsystems by the nc_close() call.
 *
 */

/**
 * \page nacm NETCONF Access Control Module (NACM)
 *
 * [RFC6536]: http://tools.ietf.org/html/rfc6536 "RFC 6536"
 * [NACMExamples]: http://tools.ietf.org/html/rfc6536#appendix-A
 *
 * NACM is a transparent subsystem of libnetconf. It is activated using
 * #NC_INIT_NACM flag in the nc_init() function. No other action is required
 * to use NACM in libnetconf. All NACM rules and settings are controlled via
 * standard NETCONF operations since NACM subsystem provides implicit datastore
 * accessible with the ncds_apply_rpc() function with id parameter set to value
 * #NCDS_INTERNAL_ID (0).
 *
 * libnetconf supports usage of the system groups (/etc/group) in the access
 * control rule-lists. To disable this feature, <enable-external-groups> value
 * must be set to false:
 *
 * ~~~~~~~
 * <nacm xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-acm">
 *   <enable-external-groups>false</enable-external-groups>
 * </nacm>
 * ~~~~~~~
 *
 *
 * ### Recovery Session ###
 * \anchor nacm-recovery
 *
 * Recovery session serves to set up initial access rules or to repair a broken
 * access control configuration. If a session is recognized as recovery NACM
 * subsystem is completely bypassed.
 *
 * By default, libnetconf consider all sessions of user with the system UID
 * equal zero as recovery. To change this default value to a UID of any user,
 * use  configure's \ref configure-nacm-recovery "--with-nacm-recovery-uid"
 * option.
 *
 *
 * ### Initial operation ###
 *
 * According to [RFC 6536][RFC6536], libnetconf's NACM subsystem is initially
 * set to allow reading (permitted read-default), refuse writing (denied
 * write-default) and allow operation execution (permitted exec-default).
 *
 * \note Some operations or data have their specific access control settings
 * defined in their data models. These settings overrides described default
 * settings.
 *
 * To change this initial settings, user has to access NACM datastore via
 * recovery session (since any write operation is denied) and set required
 * access control rules.
 *
 * For example, to change default write rule from deny to permit, use
 * edit-config operation to create (merge) the following configuration data:
 *
 * ~~~~~~~
 * <nacm xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-acm">
 *   <write-default>permit</write-default>
 * </nacm>
 * ~~~~~~~
 *
 * To guarantee all access rights to a specific users group, use edit-config
 * operation to create (merge) the following rule:
 *
 * ~~~~~~~
 * <nacm xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-acm">
 *   <rule-list>
 *     <name>admin-acl</name>
 *     <group>admin</group>
 *     <rule>
 *       <name>permit-all</name>
 *       <module-name>*</module-name>
 *       <access-operations>*</access-operations>
 *       <action>permit</action>
 *     </rule>
 *   </rule-list>
 * </nacm>
 * ~~~~~~~
 *
 * More examples can be found in the [Appendix A. of RFC 6536][NACMExamples].
 */

/**
 * \page transapi Transaction API (transAPI)
 *
 * Libnetconf transAPI is a framework that will save developers time and let them
 * focus on configuring and managing their device instead of fighting with NETCONF
 * protocol.
 *
 * It allows developer to choose parts of configuration that can be easily configured
 * as a single block. Based on list of so called 'sensitive paths' generator creates
 * C code containing single callback function for every 'sensitive path'. Whenever
 * something changes in configuration file appropriate callback function is called
 * and should reflect configuration changes to device behavior.
 *
 * Additionaly, transAPI provides opportunity to implement behavior of RPC messages.
 * If you provide 'generator.py' with data model containing definition of RPC messages
 * it will generate callbacks for them too. Whenever server receives specified RPC message
 * it will call function implemented in module.
 *
 * ### Getting started ###
 *
 * -# Create folder for your new module and enter it.
 * -# Run python script 'generator.py'. Description of parameters follows:
 *              - \-\-name  Name of your module. This is only one mandatory parameter.
 *              - \-\-paths File with list of sensitive path. One per line.
 *              - \-\-model File holding data model. Used for generating rpc callbacks.
 *              - \-\-template\-dir Path to directory with template files for generator.
 *              - \-\-with\-libxml2 Use libxml2 structures for passing data to and returning from callbacks.
 *              - \-\-without\-init Module does not need initialization when loaded.
 *              - \-\-without\-close Module does not need closing before unloaded.
 * -# Open generated .c file and fill function bodies with code that has to be run
 *        when configuration change or RPC message is received. You can add as many
 *        auxiliary function as you need.
 *        Do NOT modify structures or function headers.
 * -# Fill body of get_state_data() function so it returns all device state data
 *         defined in device data model.
 * -# Fill bodies of init and close functions (if your module needs them).
 * -# Run sequence 'autoreconf && ./configure && make'. If the build is succesful
 *        shared library is generated. You can use it with libnetconf for configuring
 *        your device.
 *
 * \subpage transapiTutorial
 */
/**
 * \page transapiTutorial transAPI Tutorial
 *
 * On this page we will show how to write simple module
 * for controlling [example toaster](http://netconfcentral.org/modulereport/toaster).
 * \note To install libnetconf follow instruction on \ref install page.
 *
 * ## Preparations ##
 *
 * First we need to identify important parts of configuration data.
 * Since toaster data model describes only one configurable element
 * we have easy choice.
 * So we can create the 'paths_file' file containing specification of our
 * chosen element and mapping prefixes with URIs for any used namespace.
 *
 * Our file may look like this (irrespective of order):
 * ~~~~~~~{.xml}
 * toaster=http://netconfcentral.org/ns/toaster
 * /toaster:toaster
 * ~~~~~~~
 *
 * If we want (we do) to create call back functions for RPC messages
 * defined for toaster we need its data model in YIN format. Data model
 * in YANG format can be downloaded from NETCONF CENTRAL (<http://dld.netconfcentral.org/src/toaster@2009-11-20.yang>).
 * Then we convert model in YANG to YIN using [pyang](http://code.google.com/p/pyang/).
 *
 * ~~~~~~~{.sh}
 * $ pyang -f yin -o toaster@2009-11-20.yin toaster@2009-11-20.yang
 * ~~~~~~~
 *
 * ## Generating code ##
 *
 * -# Create new directory for toaster module:
 * ~~~~~~~{.sh}
 * $ mkdir toaster && cd toaster/
 * ~~~~~~~
 * -# Run `generator.py':
 * ~~~~~~~{.sh}
 * $ lnc-creator --name toaster --paths paths_file --model toaster@2009-11-20.yin --with-libxml2
 * ~~~~~~~
 *
 * ## Filling up functionality ##
 *
 * Here we show the simplest example of toaster simulating module.
 * It is working but does not deal with multiple access and threads correctly.
 * Better example may be seen in git repository (examples/server/transapi/toaster.c).
 *
 * -# Open 'toaster.c' file with you favorite editor:
 * ~~~~~~~{.sh}
 * $ vim toaster.c
 * ~~~~~~~
 *
 * -# Add global variables and auxiliary functions
 * ~~~~~~~{.c}
 * enum {ON, OFF, BUSY} status;
 * pthread_t thread;
 *
 * void * auxiliary_make_toast(void * time)
 * {
 * 	sleep(*(int*)time);
 *
 * 	if (status == BUSY) {
 * 		status = ON;
 * 		ncntf_event_new(-1, NCNTF_GENERIC, "<toastDone><toastStatus>done</toastStatus></toastDone>");
 * 	}
 * 	return(NULL);
 * }
 * ~~~~~~~
 * -# Complete 'init()' function with actions that will be run right after module loads and before any other function in module is called.
 * ~~~~~~~{.c}
 * int init()
 * {
 * 	status = OFF;
 * 	printf("Toaster initialized!\n");
 * 	return(EXIT_SUCCESS);
 * }
 * ~~~~~~~
 * -# Locate 'close()' function and fill it with actions that will be run just before the module unloads. No other function will be called after 'close()'.
 * ~~~~~~~{.c}
 * void close()
 * {
 * 	printf("Toaster ready for unplugging!\n");
 * }
 * ~~~~~~~
 * -# Fill 'get_state_data()' function with code that will generate state information as defined in data model.
 * ~~~~~~~{.c}
 * char * get_state_data(char * model, char * running, struct nc_err **err)
 * {
 * 	return strdup("<?xml version="1.0"?><toaster xmlns="http://netconfcentral.org/ns/toaster"> ... </toaster>");
 * }
 * ~~~~~~~
 * -# Complete configuration callbacks. The 'op' parameter may be
 * 		used to determine operation which was done with the node. Parameter 'node' holds a
 * 		copy of node after change (or before change if op == XMLDIFF_REM).
 * ~~~~~~~{.c}
 * int callback_toaster_toaster (XMLDIFF_OP op, xmlNodePtr node, void ** data)
 * {
 * 	switch(op) {
 * 	case XMLDIFF_ADD:
 * 		status = ON;
 * 		break;
 * 	case XMLDIFF_REM:
 * 		status = OFF;
 * 		break;
 * 	default:
 * 		return(EXIT_FAILURE);
 * 	}
 * 	return(EXIT_SUCCESS);
 * }
 * ~~~~~~~
 * -# Fill RPC message callback functions with code that will be run when message arrives.
 * ~~~~~~~
 * nc_reply * rpc_make_toast (xmlNodePtr input[])
 * {
 * 	xmlNodePtr toasterDoneness = input[0];
 *	xmlNodePtr toasterToastType = input[1];
 *
 *	nc_reply * reply;
 *	int doneness = atoi(xmlNodeGetContent(toasterDoneness));
 *
 * 	if (status == ON) {
 * 		status = BUSY;
 * 		pthread_create(&thread, NULL, auxiliary_make_toast, (void*)&doneness);
 * 		pthread_detach(thread);
 * 		reply = nc_reply_ok();
 * 	} else {
 * 		reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
 * 	}
 * 	return(reply);
 * }
 * ~~~~~~~
 * ~~~~~~~
 * nc_reply * rpc_cancel_toast (xmlNodePtr input[])
 * {
 *	nc_reply * reply;
 *
 *	if (status == BUSY) {
 *		status = ON;
 *		ncntf_event_new(-1, NCNTF_GENERIC, "<toastDone><toastStatus>canceled</toastStatus></toastDone>");
 *		reply = nc_reply_ok();
 *	} else {
 *		reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
 *	}
 *	return(reply);
 * }
 * ~~~~~~~
 *
 * ## Compiling module ##
 *
 * Following sequence of commands will produce shared library 'toaster.so' which may be loaded into libnetconf:
 * ~~~~~~~{.sh}
 * $ autoreconf
 * $ ./configure
 * $ make
 * ~~~~~~~
 *
 * ## Integrating to example server ##
 *
 * In server we use libnetconfs function ncds_new_transapi() instead of ncds_new() to create transAPI capable data store.
 * Then you do not need to process any data-writing (edit-config, copy-config, delete-config, lock, unlock) data-reading (get, get-config)
 * and module data model defined RPC operations. All these operation are processed inside ncds_apply_rpc2all() function.
 */

/**
 * \defgroup genAPI General functions
 * \brief libnetconf's miscellaneous functions.
 */

/**
 * \defgroup session NETCONF Session
 * \brief libnetconf's functions for handling NETCONF sessions.
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
 * \defgroup store Datastore operations
 * \brief libnetconf's functions for handling NETCONF datastores.
 */

/**
 * \defgroup withdefaults With-defaults capability
 * \brief libnetconf's settings for NETCONF :with-defaults capability as
 * defined in RFC 6243.
 */

/**
 * \defgroup url URL capability
 * \brief libnetconf's settings for NETCONF :url capability as
 * defined in RFC 6241.
 */

/**
 * \defgroup notifications NETCONF Event Notifications
 * \brief libnetconf's implementation of NETCONF asynchronous message delivery
 * as defined in RFC 5277.
 */

/**
 * \defgroup transapi Transaction API
 * \brief libnetconf's implementation of transaction-based partial device reconfiguration.
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
#include "transapi/transapi.h"

#ifndef DISABLE_NOTIFICATIONS
#  include "notifications.h"
#endif

#ifndef DISABLE_URL
#  include "url.h"
#endif

#endif /* LIBNETCONF_H_ */
