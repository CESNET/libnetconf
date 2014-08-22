## @package netconf
# The netconf is a libnetconf wrapper for Python.
#

##
# @mainpage About
#
# [RFC6241]: http://tools.ietf.org/html/rfc6241 "RFC 6241"
# [RFC6242]: http://tools.ietf.org/html/rfc6242 "RFC 6242"
#
# libnetconf is a NETCONF library in C intended for building NETCONF clients
# and servers. It provides basic functions to connect NETCONF client and server
# to each other via SSH or TLS, to send and receive NETCONF messages and to
# store and work with the configuration data in a datastore.
#
# This is the libnetconf's API for Python.
#
# @section about-install Compilation and Installation
#
# The libnetconf Python bindings is part of the libnetconf library. To enable
# this part of the libnetconf project, just run the main `configure` script with
# the `--with-pyapi` option. Optionally, you can specify path to the required
# python3 binary:
#
# ~~~~~~~~~~~~~~
# $ ./configure --with-pyapi=/home/user/bin/python3
# $ make
# # make install
# ~~~~~~~~~~~~~~ 
#
# @subsection about-install-reqs Requirements
#
# The libnetconf Python API provides the Python module compatible with Python 3.
# To install it, the installation process requires the `distutils` Python module.
#
# @section about-license BSD License
#
# Copyright (c) 2014 CESNET, z.s.p.o.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the Company nor the names of its contributors
#    may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL) version 2 or later, in which case the provisions
# of the GPL apply INSTEAD OF those given above.
#
# This software is provided "as is", and any express or implied
# warranties, including, but not limited to, the implied warranties of
# merchantability and fitness for a particular purpose are disclaimed.
# In no event shall the company or contributors be liable for any
# direct, indirect, incidental, special, exemplary, or consequential
# damages (including, but not limited to, procurement of substitute
# goods or services; loss of use, data, or profits; or business
# interruption) however caused and on any theory of liability, whether
# in contract, strict liability, or tort (including negligence or
# otherwise) arising in any way out of the use of this software, even
# if advised of the possibility of such damage.
#
#
# ![CESNET, z.s.p.o.](cesnet-logo-125.png)
#

##
# @page apps Example Applications
#
# Example applications using the libnetconf Python API are located in the source
# tree inside the `libnetconf/python/examples/` directory.
#
# @section apps-get get.py
#
# Simple client-side application printing the result of the NETCONF \<get\>
# operation.
# ~~~~~~~~~~~~~~
# $ ./get.py localhost -f "<nacm><rule-list/></nacm>"
# <nacm xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-acm">
#   <rule-list>
#     <name>permit-all</name>
#     <group>users</group>
#     <rule>
#       <name>permit-all-rule</name>
#       <module-name>*</module-name>
#       <access-operations>*</access-operations>
#       <action>permit</action>
#     </rule>
#   </rule-list>
# </nacm>
# ~~~~~~~~~~~~~~ 
#
# @section apps-server server.py
#
# Very simple (4 LOC) NETCONF server `server.py` is alternative to the
# Netopeer's <a href="https://code.google.com/p/netopeer/wiki/SingleLevelServer">single-layer
# server</a>.
#
# To see, how the server interacts with a client, you can run it from the
# command line and communicate with the server interactively (if you don't
# understand the following lines, and you want to, read <a href="http://tools.ietf.org/html/rfc6241">RFC 6241</a>
# and <a href="http://tools.ietf.org/html/rfc6242">RFC 6242</a>):
# ~~~~~~~~~~~~~~
# $ ./server.py 
# <?xml version="1.0" encoding="UTF-8"?>
# <hello xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
#   <capabilities>
#     <capability>urn:ietf:params:netconf:base:1.0</capability>
#     <capability>urn:ietf:params:netconf:base:1.1</capability>
#     <capability>urn:ietf:params:netconf:capability:writable-running:1.0</capability>
#     ...
#   </capabilities>
#   <session-id>31059</session-id>
# </hello>
# ]]>]]><?xml version="1.0" encoding="UTF-8"?>
# <hello xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
#   <capabilities>
#     <capability>urn:ietf:params:netconf:base:1.0</capability>
#   </capabilities>
# </hello>]]>]]>
# <?xml version="1.0" encoding="UTF-8"?>
# <rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="1">
#   <close-session/>
# </rpc>]]>]]>
# <?xml version="1.0" encoding="UTF-8"?>
# <rpc-reply xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="1">
#   <ok/>
# </rpc-reply>
# ]]>]]>
# ~~~~~~~~~~~~~~
#
# However, the more usual way how to use it, is to set your SSH server to run
# it as its `netconf` SSH Subsystem. In this case, the server is started 
# automatically when client connects to the host. To configure your SSH server
# this way, add the following line to the `/etc/ssh/sshd_config` file.
# ~~~~~~~~~~~~~~
# Subsystem netconf /path/to/server.pl
# ~~~~~~~~~~~~~~
#
# Also remember to correctly set the port where the SSH server listens. By
# default, it listens on port 22, but NETCONF has assigned port 830 and the most
# of clients use it by default.
#
# And finally, don't forget to make the SSH server reload the changed
# configuration.
#

## NETCONF version constant.
#
# The identifier for version 1.0 of the NETCONF protocol in the :meth:`connect`
# method.
NETCONFv1_0 = "urn:ietf:params:netconf:base:1.0"

## NETCONF version constant.
#
# The identifier for version 1.1 of the NETCONF protocol in the :meth:`connect`
# method.
NETCONFv1_1 = "urn:ietf:params:netconf:base:1.1"

## NETCONF transport protocol constant.
#
# The identifier for NETCONF over SSH transport.
TRANSPORT_SSH = "ssh"

## NETCONF transport protocol constant.
#
# The identifier for NETCONF over TLS transport.
TRANSPORT_TLS = "tls"

## NETCONF :with-defaults capability constant.
#
# :with-default's 'report-all' retrieval mode. All data nodes are reported
# including any data nodes considered to be default data.
WD_ALL = 0x0001

## NETCONF :with-defaults capability constant.
#
# :with-default's 'trim' retrieval mode. Data nodes containing the schema
# default value are not reported.
WD_TRIM = 0x0010

## NETCONF :with-defaults capability constant.
#
# :with-default's 'explicit' retrieval mode. Only data nodes explicitly set by
# the client (including those set to its schema default value) are reported.
WD_EXPLICIT = 0x0100

## NETCONF :with-defaults capability constant.
#
# :with-default's 'report-all-tagged' retrieval mode. Same as the 'report-all'
# mode except the data nodes containing default data include an XML attribute
# indicating this condition.
WD_ALL_TAGGED = 0x1000

## NETCONF datastores constant.
#
# NETCONF running datastore.
RUNNING = 3

## NETCONF datastores constant.
#
# NETCONF startup datastore. Available only if the NETCONF :startup capability
# is supported by the server.
STARTUP = 4

## NETCONF datastores constant.
#
# NETCONF candidate datastore. Available only if the NETCONF :candidate
# capability is supported by the server.
CANDIDATE = 5

## Set libnetconf verbose level.
#
# When initiated, it starts with value 0 (show only errors).
#
# @param level Verbosity level, allowed values are as follows:
# - 0 (errors)
# - 1 (warnings)
# - 2 (verbose)
# - 3 (debug)
def setVerbosity(level):
    pass

## Enable/disable logging using syslog.
#
# @param enabled True/False to enable/disable logging (enabled by default).
# @param name The text prepended to every message, typically the program name.
#        If None, the program name is used.
# @param option Flags which control the operation of openlog(3).
# @param facility Specifies what type of program is logging the message. For
#        details see openlog(3) man page. 
def setSyslog(enabled, name = None, option = LOG_PID, facility = LOG_DAEMON):
    pass

## Set the list of default NETCONF capabilities used for all actions where required.
#
# @param list The list of strings with the supported NETCONF capabilities. By
# default, this list is provided by libnetconf containing all capbilities
# supported by libnetconf.
def setCapabilities(list):
    pass

## Get the list of default NETCONF capabilities used for all actions where required.
#
# @return The list of strings with the currently supported capabilities identifiers.
def getCapabilities():
    pass

## Set a reference to a data model (in the YIN format) that is required (e.g.
# imported) by some other data model.
#
# All required models must be provided before creating the datastore using
# addDatastore() function.
#
# @param model The path to the data model file. The data model must be in the
#        YIN format.
# @param features Optional list of enabled features in this data model. By
#        default, all features are enabled. Empty list disables all features of
#        the model. 
def addModel(model, features = None):
    pass

## Add support of the configuration data defined in the provided data model to
# the datastore.
#
# @param model The path to the data model in the YIN format.
# @param datastore The path to the file where the configuration data related to
#        this data model will be stored. The file does not need exist, in that
#        case it will be created automatically. If the argument is not
#        specified, configuration data of the data model are not handled (e.g.
#        some data models can define only state data).
# @param transapi The path to an optional libnetconf transAPI module (.so)
#        implementing the data model of the datastore.
# @param features The list of enabled features of this data model. By default,
#        all features are enabled. Empty list disables all features of the model.
def addDatastore(model, datastore = None, transapi = None, features = None):
    pass

## Add augmenting data model.
#
# The arguments have the same meanings as for the addDatastore() function except
# the missing datastore argument. It is no more needed since the configuration
# data are placed into the datastore files of the augmented datastores.
#
# @param model The path to the data model in the YIN format.
# @param transapi The path to an optional libnetconf transAPI module (.so)
#        implementing the augmenting data model.
# @param features The list of enabled features of this data model. By default,
#        all features are enabled. Empty list disables all features of the model.
def addAugment(model, transapi = None, features = None):
    pass

## Representation of a connection between a NETCONF server and client.
#
# The constructor of the class can be used in two ways: a client side way (the
# host argument is set) and a server side way (the host argument is None). There
# are also class methods connect() and accept() making usage of both approaches
# easier.
class Session:
    ## Session constructor 
    #
    # The parameters have different meaning for calling from client and server.
    # Client side constructor requires the host argument while the server side
    # constructor expects the host argument to be None. The Session class
    # provides connect() and accept() class functions for easier usage of the
    # constructor.
    #
    # For the client side, if the fd_in and fd_out arguments are specified,
    # arguments host, port, user and transport are only informative. They are
    # not used to establish a connection. In this case the function starts with
    # writing the NETCONF \<hello\> message to the fd_out and expecting the
    # server's \<hello\> message (in plain text) in fd_in.
    #
    # For the server side, these arguments are set STDIN_FILENO and
    # STDOUT_FILENO by default.
    #
    # @param host Client side mandatory argument specifying a domain name or an
    #        IP address of the NETCONF server host where to connect.
    # @param port Port where to connect, if not specified, default NETCONF port
    #        value 830 is used.
    # @param user Username of the session holder. For the client side, it is
    #        used to connect to the remote host, for the server side it is used
    #        to get correct access rights. In both cases, if not specified, it
    #        is extracted from the process UID.
    # @param transport NETCONF transport protocol specified as one of the
    #        NETCONF transport protocol constants.
    # @param capabilities The list of NETCONF capabilities announced to the
    #        other side. By default, the internal list, as set by
    #        setCapabilities() function.
    # @param fd_in File descriptor for reading NETCONF data from.
    # @param fd_out File descriptor for writing NETCONF data to.
    # @return Created NETCONF Session object.
    def __init__(host = None, port = 830, user = None, transport = netconf.TRANSPORT_SSH, capabilities = None, fd_in = -1, fd_out = -1):
        self.id = 1;
        self.host = port;
        self.port = port;
        self.user = user;
        self.transport = transport;
        self.version = "";
        self.capabilities = [];
        pass
    
    ## @property id
    # NETCONF %Session id. Read-only.
    
    ## @property host
    # Host where the Session is connected.

    ## @property port
    # Port number where the Session is connected.

    ## @property user
    # Username of the user connected with the Session.

    ## @property transport
    # Transport protocol used for the Session.

    ## @property version
    # NETCONF Protocol version used for the Session.

    ## @property capabilities
    # List of NETCONF capabilities assigned to the Session.
   
    ## Placeholder for the client side session constructor.
    #
    # The Session class function.
    #
    # When the fd_in and fd_out arguments are specified, arguments host, port,
    # user and transport are only informative. They are not used to establish
    # a connection. In this case the function starts with writing the NETCONF
    # \<hello\> message to the fd_out and expecting the server's \<hello\>
    # message (in plain text) in fd_in.
    #
    # @param host Mandatory argument specifying a domain name or an IP address
    #        of the NETCONF server host where to connect.
    # @param port Port where to connect, if not specified, default NETCONF port
    #        value 830 is used.
    # @param user Username used for authentication when connecting to the remote
    #        host. If not specified, it is extracted from the process UID.
    # @param transport NETCONF transport protocol specified as one of the
    #        NETCONF transport protocol constants.
    # @param version Supported NETCONF protocol version specified as the NETCONF
    #        capability value. It can be specified as one of the NETCONF version
    #        constants netconf#NETCONFv1_0 or netconf#NETCONFv1_1. By default,
    #        the supported protocol versions are decided from the capabilities
    #        list provided by the getCapabilities() function. During the NETCONF
    #        protocol handshake the highest common protocol version for both the
    #        server and client is selected for further communication.
    # @param fd_in File descriptor for reading NETCONF data from. If set, the
    #        transport connection must be already established including the
    #        user authentication.
    # @param fd_out File descriptor for writing NETCONF data to. If set, the
    #        transport connection must be already established including the
    #        user authentication.
    # @return Created NETCONF Session object.
    def connect(host, port=830, user = None, transport = netconf.TRANSPORT_SSH, version = None, fd_in=-1, fd_out=-1):
        pass
    
    ## Placeholder for the server side session constructor.
    #
    # @param user Username of the session holder, used to get correct access
    #        rights. If not specified, it is extracted from the process UID.
    # @param capabilities The list of NETCONF capabilities announced to the
    #        client side. By default, the internal list, as set by
    #        setCapabilities() function.
    # @param fd_in File descriptor for reading NETCONF data from. By default,
    #        the NETCONF data are read from the standard input.
    # @param fd_out File descriptor for writing NETCONF data to. By default,
    #        the NETCONF data are written to the standard output.
    def accept(user = None, capabilities = None, fd_in = STDIN_FILENO, fd_out = STDOUT_FILENO):
        pass

    ## Ask if the Session is still active.
    #
    # This function is generic for both the client and the server side.
    #
    # @return True if the Session is still connected. If the Session was
    # closed (due to error or close request from the client), the False is
    # returned.
    def isActive():
        pass

    ## Perform the NETCONF \<get\> operation.
    #
    # This function is supposed for the client side only.
    #
    # @param filter Optional string representing NETCONF Subtree filter.
    # @param wd The NETCONF :with-defaults mode. Possible values are provided
    #        as the WD_* constants of the \ref netconf module.
    # @return The result data as a string.
    def get(filter = None, wd = None):
        pass

    ## Perform the NETCONF \<get-config\> operation
    #
    # This function is supposed for the client side only.
    #
    # @param source The datastore where to query. Possible values are
    #        netconf#RUNNING, netconf#STARTUP and netconf#CANDIDATE. To allow
    #        the last two values, the appropriate NETCONF capability must be
    #        supported by the connected server.
    # @param filter Optional string representing NETCONF Subtree filter.
    # @param wd The NETCONF :with-defaults mode. Possible values are provided
    #        as the WD_* constants of the \ref netconf module.
    # @return The result data as a string.
    def getConfig(source, filter = None, wd = None):
        pass

    ## Perform the NETCONF \<copy-config\> operation.
    #
    # This function is supposed for the client side only.
    #
    # @param source Source datastore of the data to copy. The value can be one
    #        of the netconf#RUNNING, netconf#STARTUP and netconf#CANDIDATE, the
    #        URL string if the Session supports the NETCONF :url capability or
    #        the complete configuration datastore content provided as a string.
    # @param target Target datastore where copy the data. The value of the
    #        argument is one of the datastore constants or the URL string if the
    #        Session supports the NETCONF :url capability.
    # @param wd The NETCONF :with-defaults mode. Possible values are provided
    #        as the WD_* constants of the \ref netconf module.
    #
    def copyConfig(source, target, wd = None):
        pass

    ## Perform the NETCONF \<delete-config\> operation.
    #
    # This function is supposed for the client side only.
    #
    # @param target Target datastore to be removed. Accepted values are the
    #        datastore constants or the URL string if the Session supports the
    #        NETCONF :url capability.
    def deleteConfig(target):
        pass
    
    ## Perform the NETCONF \<kill-session\> operation.
    #
    # This function is supposed for the client side only.
    #
    # @param id String with the ID of a NETCONF %Session to kill.
    def killSession(id):
        pass
    
    ## Lock the specified NETCONF datastore.
    #
    # This function is supposed for the client side only.
    #
    # @param target The datastore to lock, accepted values are the datastore
    #        constants.
    def lock(target):
        pass

    ## Unlock the specified NETCONF datastore.
    #
    # This function is supposed for the client side only.
    #
    # @param target The datastore to lock, accepted values are the datastore
    #        constants.
    def unlock(target):
        pass
    
    ## Unlock the specified NETCONF datastore.
    #
    # This function is supposed for the server side only.
    #
    # It automatically process the next request from the NETCONF client
    # connected via the Session. After the processing RPC, the caller should
    # check if the Session wasn't closed using the isActive() method.
    def processRequest():
        pass
