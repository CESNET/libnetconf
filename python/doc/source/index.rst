.. netconf documentation master file, created by
   sphinx-quickstart on Mon Aug 11 13:59:29 2014.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. toctree::
   :maxdepth: 2
   
:mod:`netconf` --- NETCONF Protocol Implementation
==================================================

.. module:: netconf
   :synopsis: NETCONF Protocol Implementation.
.. moduleauthor:: Radek Krejci <rkrejci@cesnet.cz>
.. sectionauthor:: Radek Krejci <rkrejci@cesnet.cz>

The :mod:`netconf` is a libnetconf wrapper for Python. There is currently only
the :class:`Session`.

Module Constants
----------------

- NETCONF version constants:

.. data:: NETCONFv1_0

   The identifier for version 1.0 of the NETCONF protocol in the :meth:`connect`
   method.

.. data:: NETCONFv1_1

   The identifier for version 1.1 of the NETCONF protocol in the :meth:`connect`
   method.

- NETCONF transport protocol constants:

.. data:: TRANSPORT_SSH

   The identifier for NETCONF over SSH transport. :const:`TRANSPORT_SSH` is ``ssh``.

.. data:: TRANSPORT_TLS

   The identifier for NETCONF over TLs transport. :const:`TRANSPORT_TLS` is ``tls``.

- NETCONF *:with-defaults* capability constants:

.. data:: WD_ALL

   *:with-default*'s ``report-all`` retrieval mode. All data nodes are reported
   including any data nodes considered to be default data.

.. data:: WD_ALL_TAGGED

   *:with-default*'s ``report-all-tagged`` retrieval mode. Same as the
   ``report-all`` mode except the data nodes containing default data include an
   XML attribute indicating this condition.

.. data:: WD_TRIM

   *:with-default*'s ``trim`` retrieval mode. Data nodes containing the schema
   default value are not reported.

.. data:: WD_EXPLICIT

   *:with-default*'s ``explicit`` retrieval mode. Only data nodes explicitly
   set by the client (including those set to its schema default value) are
   reported.
   
- NETCONF datastores constants:

.. data:: RUNNING

   NETCONF *running* datastore.

.. data:: STARTUP

   NETCONF *startup* datastore. Available only if the NETCONF *:startup*
   capability is supported by the server.

.. data:: CANDIDATE

   NETCONF *candidate* datastore. Available only if the NETCONF *:candidate*
   capability is supported by the server.

The module provides the following functions:

.. function:: setVerbosity(level)

   Set libnetconf verbose level. When initiated, it starts with value 0 (show
   only errors). Allowed values are as follows:

- 0 (errors)
- 1 (warnings)
- 2 (verbose)
- 3 (debug)

.. function:: setSyslog(enabled[, name, option=LOG_PID, facility=LOG_DAEMON])

   Enable/disable logging using syslog (enabled by default). If enabled is
   ``True``, other parameters can be optionally set with the meanings
   corresponding to the *openlog(3)* function.

.. function:: setCapabilities(list)

   Set the list of default NETCONF capabilities used for all actions where
   required. The *list* argument is a list of strings with the supported NETCONF
   capabilities. By default, this list is provided by libnetconf containing all
   capbilities supported by libnetconf.

.. function:: getCapabilities()

   Returns the list of default NETCONF capabilities used for all actions where
   required.

.. function:: addModel(model[,features=None])

   Set a reference to a data model (in the ``YIN`` format) that is required (e.g.
   imported) by some other data model. All required models must be provided
   before creating the datastore using :func:`addDatastore` function.
   
   The *model* argument provides the path to the data model file. The data model
   must be in the ``YIN`` format. Optional argument *features* is the list of
   enabled features in this data model. By default, all features are enabled.
   Empty list disables all features of the model.

.. function:: addDatastore(model[, datastore=None, transapi=None, features=None])

   Add support of the configuration data defined in the provided data model to
   the datastore. The path to the data model in the ``YIN`` format is provided
   ast mandatory argument *model*. All other arguments are optional.
   
   The *datastore* argument specifies path to the file where the configuration
   data related to this data model will be stored. The file does not need
   exist, in that case it will be created automatically. If the argument is not
   specified, configuration data of the data model are not handled (e.g. some
   data models can define only state data).
   
   The *transapi* arguments provides path of the libnetconf transAPI module
   (.so) implementing the data model of the datastore.
   
   The last optional argument *features* provides the list of enabled features
   of this data model. By default, all features are enabled. Empty list disables
   all features of the model.

.. function:: addAugment(model[, transapi=None, features=None])

   Add augmenting data model. The arguments have the same meanings as for the
   :func:`addDatastore` function except the missing *datastore* argument. It is
   no more needed since the configuration data are placed into the datastore
   files of the augmented datastores.


:class:`Session` Class
----------------------

A :class:`Session` object represents a connection between a NETCONF server and
client. The constructor of the class can be used in two ways: a client side way
(the *host* argument is set) and a server side way (the *host* argument is
``None``). There are also class methods :meth:`connect` and
:meth:`accept` making usage of both approaches easier.

.. class:: Session(host[, port=830, user=None, transport=netconf.TRANSPORT_SSH, capabilities=None])

   Constructor for a client side application. 
   
   The *host* argument must be specified as a domain name or an IP address of
   the NETCONF server host. If not specified, default NETCONF *port* value 830 
   is used and the *user* is extracted from the process UID. Besides the default
   NETCONF over SSH transport, the NETCONF over TLS transport can be requested
   using ``netconf.TRANSPORT_TLS`` constant.
   
   As the last argument applicable at the client side, caller can specify the
   list of NETCONF capabilities announced to the server. By default, the
   internal list provided by libnetconf is used. This way the both NETCONF
   versions, 1.0 and 1.1 are supported (with preference to version 1.1). 

.. class:: Session([user=None, capabilities=None, fd_in=STDIN_FILENO, fd_out=STDOUT_FILENO])

   Constructor for a server side application.
   
   All arguments are optional. By default, *user* is extracted from the process
   UID and *capabilities* list is the same as the list provided by the
   :func:`getCapabilities` function.
   
   The *fd_in* and *fd_out* are file descriptors where the libnetconf will read
   the unencrypted data from and write unencrypted data to when communicating
   with the transport protocol server (SSH or TLS server).

.. classmethod:: Session.connect(host[, port=830, user=None, transport=netconf.TRANSPORT_SSH, version=None)

   Same as the :class:`Session` class constructor for the client side except the
   last parameter. Here the shortcuts ``netconf.NETCONFv1_0`` and
   ``netconf.NETCONFv1_1`` can be used. By default, the supported protocol
   versions are decided from the capabilities list provided by the 
   :func:`getCapabilities` function. During the NETCONF protocol handshake the
   highest common protocol version for both the server and client is selected
   for further communication.
   
.. classmethod:: Session.accept([user=None, capabilities=None, fd_in=STDIN_FILENO, fd_out=STDOUT_FILENO])

   Same as the :class:`Session` class constructor for the server side.


Instance attributes (read-only):

.. attribute:: Session.id

   NETCONF Session id.

.. attribute:: Session.host

   Host where the NETCONF Session is connected.
   
.. attribute:: Session.port

   Port number where the NETCONF Session is connected.

.. attribute:: Session.user

   Username of the user connected with the NETCONF Session.

.. attribute:: Session.transport

   Transport protocol used for the NETCONF Session.

.. attribute:: Session.version

   "NETCONF Protocol version used for the NETCONF Session.
   
.. attribute:: Session.capabilities

   List of NETCONF capabilities assigned to the NETCONF Session.


Instance methods:

.. method:: Session.get([filter=None, wd=None])

   Performs NETCONF <get> operation and returns the returned data as a string.
   
   *filter* is optional string representing NETCONF Subtree filter. *wd*
   argument can be used to specify the NETCONF *:with-defaults* mode - possible
   values are provided as ``WD_*`` constants of the :mod:`netconf` module.

.. method:: Session.getConfig(source[, filter=None, wd=None])

   Performs NETCONF <get-config> operation and returns the returned data as a
   string. Requires specification of the source datastore where to query. Possible
   values are provided as the :mod:`netconf` module constants ``RUNNING``,
   ``STARTUP`` and ``CANDIDATE``. To allow the last two values, the appropriate
   NETCONF capability must be supported by the server.
   
   Optional parameters *filter* and *wd* are the same as for the :meth:`get`
   method.

.. method:: Session.copyConfig(source, target[, wd=None])

   Performs NETCONF <copy-config> operation. The *source* can be one of the
   :mod:`netconf` module datastore constants, the URL string if the session
   supports the NETCONF *:url* capability or the complete configuration
   datastore provided as a string. The *target* argument is one of the
   :mod:`netconf` module datastore constants or the URL string if the session
   supports the NETCONF *:url* capability.

   Optional *wd* argument can be used to specify the NETCONF *:with-defaults*
   mode - possible values are provided as ``WD_*`` constants of the
   :mod:`netconf` module.

.. method:: Session.deleteConfig(target)

   Performs NETCONF <delete-config> operation removing the specified datastore.
   The *target* argument can be one of the :mod:`netconf` module datastore
   constants or the URL string if the session supports the NETCONF *:url*
   capability.

.. method:: Session.killSEssion(id)

   Performs NETCONF <kill-session> operation. Mandatory parameter *id* contains
   string with the ID of a NETCONF session to kill.

.. method:: Session.lock(target)

   Lock the specified NETCONF datastore. Possible values for the *target*
   parameter are provided as the :mod:`netconf` module constants ``RUNNING``,
   ``STARTUP`` and ``CANDIDATE``. To allow the last two values, the appropriate
   NETCONF capability must be supported by the server.

.. method:: Session.unlock(target)

   The reverse operation to the :meth:`lock` method.
   
