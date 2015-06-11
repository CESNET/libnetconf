**libnetconf** is a NETCONF library in C intended for building NETCONF clients and servers. It provides basic functions to connect NETCONF client and server to each other via SSH, to send and receive NETCONF messages and to store and work with the configuration data in a datastore.

libnetconf implements the NETCONF protocol introduced by IETF. More information about NETCONF protocol can be found at [NETCONF WG](http://trac.tools.ietf.org/wg/netconf/trac/wiki).

libnetconf is maintained and further developed by the [Tools for Monitoring and Configuration](https://www.liberouter.org/) department of [CESNET](http://www.ces.net/). Any testing of the library is welcome. Please inform us about your experiences with using libnetconf via our [mailing list](https://groups.google.com/forum/#!forum/libnetconf) or the Issue section. Any feature suggestion or bugreport is also appreciated.

&lt;wiki:gadget url="https://www.ohloh.net/p/709693/widgets/project\_factoids\_stats.xml" height="270" width="800" border="0"/&gt;

<a href='Hidden comment: 
== What You Can Find Here ==

Besides libnetconf itself, also example implementation of the command line client application using libnetconf can be found in the repository. In the future, example NETCONF server implementing general configuration capabilities of a Linux box will be added.
'></a>

## Features ##

  * **Implemented**
    * NETCONF v1.0 and v1.1 compliant ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF over SSH ([RFC 6242](http://tools.ietf.org/html/rfc6242)) including Chunked Framing Mechanism
      * DNSSEC SSH Key Fingerprints ([RFC 4255](http://tools.ietf.org/html/rfc4255))
    * NETCONF over TLS ([RFC 5539bis](http://tools.ietf.org/html/draft-ietf-netconf-rfc5539bis-05))
    * NETCONF Writable-running capability ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF Candidate configuration capability ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF Validate capability ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF Distinct startup capability ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF URL capability ([RFC 6241](http://tools.ietf.org/html/rfc6241))
    * NETCONF Event Notifications ([RFC 5277](http://tools.ietf.org/html/rfc5277) and [RFC 6470](http://tools.ietf.org/html/rfc6470))
    * NETCONF With-defaults capability ([RFC 6243](http://tools.ietf.org/html/rfc6243))
    * NETCONF Access Control ([RFC 6536](http://tools.ietf.org/html/rfc6536))
    * NETCONF Call Home ([Reverse SSH draft](http://tools.ietf.org/html/draft-ietf-netconf-reverse-ssh-05), [RFC 5539bis](http://tools.ietf.org/html/draft-ietf-netconf-rfc5539bis-05))
    * [Python bindings](http://libnetconf.googlecode.com/git/doc/python/html/index.html)

  * **Future**
    * multi-threading support

## Interoperability ##

In November 2012, prior to the IETF 85 meeting, libnetconf was one of the NETCONF protocol implementation participating in [NETCONF Interoperability Testing](http://www.internetsociety.org/articles/successful-netconf-interoperability-testing-announced-ietf-85).

## Documentation ##

libnetconf's API documentation including a description of its use is available [here](http://libnetconf.googlecode.com/git/doc/doxygen/html/index.html).

### Papers and Articles ###

  * _Building NETCONF-enabled Network Management Systems with libnetconf_ at the IFIP/IEEE International Symposium on Integrated Network Management 2013 (IM2013), [paper](http://wiki.libnetconf.googlecode.com/git/papers/im2013/paper.pdf), [poster](http://wiki.libnetconf.googlecode.com/git/papers/im2013/poster.pdf).

  * _Managing SamKnows Probes using NETCONF_ at the IEEE/IFIP Network Operations and Management Symposium 2014 (NOMS 2014), [paper](http://wiki.libnetconf.googlecode.com/git/papers/noms2014/paper.pdf), [poster](http://wiki.libnetconf.googlecode.com/git/papers/noms2014/poster.pdf).

## Project History ##

libnetconf comes from the Netopeer project. The first version of Netopeer was implemented in 2007 as a bachelor thesis at Masaryk university (available only [in czech](http://is.muni.cz/th/98863/fi_b/)). Further work on the project was covered by CESNET, z.s.p.o., Czech National Research and Education Network (NREN) operator and in 2009 released as an open source project at GoogleCode.

Further development led to splitting the original Netopeer applications and creating a separate library implementing NETCONF functionality. The libnetconf library was detached as a standalone project in 2012. For some time, the [Netopeer server](https://www.liberouter.org/?page_id=827) was developed internally at CESNET to control specific CESNET devices.

In 2013, the [Netopeer project](http://code.google.com/p/netopeer/) was restarted. Currently it contains a simple example server and a command line interface. Both these applications were moved to the project from the libnetconf, where they were used as example applications. Besides them, the project includes the advanced Netopeer server, originally developed at CESNET, together with several transAPI modules covering basic configuration of the Linux server. [The Netopeer web client](https://github.com/CESNET/Netopeer-GUI) is available separately at GitHub.


## Release History ##

  * libnetconf-0.9 was released in November 2014
  * libnetconf-0.8 was released in May 2014
  * libnetconf-0.7 was released in February 2014
  * libnetconf-0.6 was released in September 2013
  * libnetconf-0.5 was released in June 2013
  * libnetconf-0.4 was released in April 2013
  * libnetconf-0.3 was released in December 2012
  * libnetconf-0.2 was released in October 2012
  * libnetconf-0.1 was released in July 2012
  * The libnetconf project was started in April 2012