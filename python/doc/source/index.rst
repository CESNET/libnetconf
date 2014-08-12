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

The :mod:`netconf` module exports the following constants:

.. data:: NETCONFv1_0

   The identifier for version 1.0 of the NETCONF protocol in the :meth:`connect`
   method.

.. data:: NETCONFv1_1

   The identifier for version 1.1 of the NETCONF protocol in the :meth:`connect`
   method.

.. data:: TRANSPORT_SSH

   The identifier for NETCONF over SSH transport. :const:`TRANSPORT_SSH` is ``ssh``.

.. data:: TRANSPORT_TLS

   The identifier for NETCONF over TLs transport. :const:`TRANSPORT_TLS` is ``tls``.


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

.. function:: getCapabilities()

   Returns the list of default NETCONF capabilities supported by libnetconf.


Available Types
---------------

.. class:: Session
   :noindex:

   NETCONF session representation. There are class methods :meth:`connect` and
   :meth:`accept` as shortcuts for the class constructors used in NETCONF
   client, respectively NETCONF server side.
   
   
:class:`Session` Objects
------------------------

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
   UID and *capabilities* list is taken from libnetconf containing all NETCONF
   capabilities and data models implemented by the libnetconf.
   
   The *fd_in* and *fd_out* are file descriptors where the libnetconf will read
   the unencrypted data from and write unencrypted data to when communicating
   with the transport protocol server (SSH or TLS server).

.. classmethod:: Session.connect(host[, port=830, user=None, transport=netconf.TRANSPORT_SSH, version=None)

   Same as the :class:`Session` class constructor for the client side except the
   last parameter. Here the shortcuts ``netconf.NETCONFv1_0`` and
   ``netconf.NETCONFv1_1`` can be used. By default, the both protocol versions
   are supported, so the highest common protocol version for both the server and
   client is selected for further communication.
   
.. classmethod:: Session.accept([user=None, capabilities=None, fd_in=STDIN_FILENO, fd_out=STDOUT_FILENO])

   Same as the :class:`Session` class constructor for the server side.


Instance attributes (read-only):

.. attribute:: Session.id

   NETCONF Session id.

.. attribute:: Session.host

   Host where the NETCONF Session is connected.
   
.. attribute Session.port

   Port number where the NETCONF Session is connected.

.. attribute Session.user

   Username of the user connected with the NETCONF Session.

.. attribute Session.transport

   Transport protocol used for the NETCONF Session.

.. attribute Session.version

   "NETCONF Protocol version used for the NETCONF Session.
   
.. attribute:: Session.capabilities

   List of NETCONF capabilities assigned to the NETCONF Session.


Instance methods:

.. method:: Session.send()
